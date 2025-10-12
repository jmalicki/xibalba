# Debugging Validation "Bugs": Is It Xibalba or the Kernel?

## The Skepticism (Healthy!)

**Claim**: Xibalba found 63,000 bugs in ext4 in 10 seconds

**Reality Check**: ext4 is battle-tested by millions of systems daily. Finding thousands of bugs would be catastrophic.

**Most Likely**: Bugs in **Xibalba's validation logic**, not ext4.

## Systematic Debugging Plan

### Phase 1: Sanity Checks (5 minutes)

#### 1.1 Check if "bugs" are all the same type
```bash
# Run test
bazel run //chaos:simple_chaos_test -- --duration 10 /tmp/test

# Analyze bug types
cat /tmp/test/xibalba-bugs.jsonl | jq '{missing, duplicates, phantoms}' | sort | uniq -c
```

**Expected**:
- If ALL bugs are same type (e.g., all "missing") → systematic validation bug
- If mixed types → might be real

#### 1.2 Check if bugs are consistent across runs
```bash
# Run 3 times
for i in {1..3}; do
  rm -rf /tmp/test$i && mkdir -p /tmp/test$i
  bazel run //chaos:simple_chaos_test -- --duration 10 /tmp/test$i
  echo "Run $i: $(grep -c '^{' /tmp/test$i/xibalba-bugs.jsonl) bugs"
done
```

**Expected**:
- Same bug count every time → deterministic Xibalba bug
- Varies widely → might be real race conditions

#### 1.3 Test without concurrency
```bash
# Single reader, single writer (minimal concurrency)
bazel run //chaos:simple_chaos_test -- --readers 1 --writers 1 --duration 10 /tmp/test

# Check bugs
cat /tmp/test/xibalba-bugs.jsonl | wc -l
```

**Expected**:
- Still thousands of bugs → validation logic is broken
- Zero bugs → concurrency triggers real races (or validation races!)

### Phase 2: Inspect Actual Bug Data (10 minutes)

#### 2.1 Look at first bug in detail
```bash
# Get first bug
head -1 /tmp/test/xibalba-bugs.jsonl | jq '.'

# Check what file was involved
cat /tmp/test/xibalba-history.json | jq '.operations[] | select(.type == "CREATE")' | head -5
```

**Questions**:
- What file is missing/phantom/duplicate?
- Does it make sense given the operations?
- Can we manually verify the ground truth?

#### 2.2 Check if it's the `lost+found` directory
```bash
# ext4 creates lost+found but we don't track it
cat /tmp/test/xibalba-bugs.jsonl | jq 'select(.phantoms > 0)' | head -1

# Check history for lost+found
cat /tmp/test/xibalba-history.json | jq '.operations[] | select(.file == "lost+found")'
```

**Hypothesis**: We're reporting `lost+found` as phantom (not in ground truth).

#### 2.3 Examine vector clock snapshots
```bash
# Run test with debug output
# (We need to add debug logging to see actual VC values)
```

**Need**: Add debug logging to print vector clocks when bugs found.

### Phase 3: Minimal Reproduction (15 minutes)

#### 3.1 Write a minimal test case
```c
// test_minimal.c
state_tracker_t *tracker = tracker_init();

// Create one file
tracker_record_create(tracker, "test.txt");
usleep(10000);

// Read and report seeing it
char* actual[] = {(char*)"test.txt"};
validation_result_t result = tracker_validate_read(
    tracker, actual, 1,
    time_now(), time_now() + 1000000,
    CONSISTENCY_WEAK_POSIX
);

printf("Bugs: %lu (should be 0!)\n", result.total_bugs_found);
```

**Expected**:
- Bugs = 0 → validation works in simple case
- Bugs > 0 → broken even in simple case!

#### 3.2 Test vector clock happens-before directly
```bash
# Add unit test
TEST_F(VectorClockTest, BasicHappensBefore) {
    vclock_tick(vc, thread_A);  // A does operation
    uint64_t vc_a[MAX_THREADS];
    vclock_snapshot(vc, vc_a);
    
    vclock_tick(vc, thread_B);  // B does operation  
    uint64_t vc_b[MAX_THREADS];
    vclock_snapshot(vc, vc_b);
    
    // A should happen-before B
    EXPECT_TRUE(vclock_happens_before(vc_a, vc_b));
}
```

#### 3.3 Test thread ID hashing
```bash
# Check if thread ID collisions
# Our hash: ((uint64_t)thread_id / 1000) % MAX_THREADS

# Hypothesis: Different threads map to same index!
```

**Suspicion**: Thread ID hash collisions could cause false causality!

### Phase 4: Root Cause Analysis

#### Hypothesis 1: Thread ID Hash Collisions

**Problem**:
```c
uint32_t idx = ((uint64_t)thread_id / 1000) % MAX_THREADS;
```

If two threads hash to same index:
- They share same vector clock slot
- False happens-before relationships!

**Test**:
```bash
# Print actual thread IDs from test
cat xibalba-bugs.jsonl | jq '.thread' | sort -u
# Count unique: should be ≤ MAX_THREADS (64)
```

**Fix**: Use proper hash table or thread-local storage.

#### Hypothesis 2: Vector Clock Not Initialized

**Problem**:
```c
file->create_vc[MAX_THREADS];  // Might be uninitialized!
```

**Test**:
```bash
# Check if has_create_vc is always true
# (We should be setting this in tracker_record_create)
```

**Fix**: Ensure memset or initialization.

#### Hypothesis 3: lost+found Not Filtered

**Problem**:
ext4 always has `lost+found` directory, but we never called `tracker_record_create(tracker, "lost+found")`.

**Result**: Every read that includes `lost+found` → phantom entry!

**Test**:
```bash
cat xibalba-bugs.jsonl | jq 'select(.phantoms > 0)' | wc -l
# If this equals total bugs → it's lost+found!
```

**Fix**: Filter out filesystem-created directories (`lost+found`, `.`, `..`).

#### Hypothesis 4: Vector Clock Happens-Before Logic Inverted

**Problem**:
```c
bool vclock_happens_before(const uint64_t *a, const uint64_t *b) {
    // Is the logic backwards?
}
```

**Test**: Unit tests should catch this (we have 15/15 passing).

**But**: Check if unit tests use same thread IDs as production.

### Phase 5: Instrumentation (30 minutes)

Add debug logging to validation:

```c
// In tracker_validate_read
if (result.total_bugs_found > 0) {
    fprintf(stderr, "DEBUG BUG FOUND:\n");
    fprintf(stderr, "  File: %s\n", file->filename);
    fprintf(stderr, "  Was read: %s\n", was_read ? "YES" : "NO");
    fprintf(stderr, "  Create VC: ");
    vclock_print(file->create_vc, MAX_THREADS);
    fprintf(stderr, "\n  Read VC: ");
    vclock_print(read_vc, MAX_THREADS);
    fprintf(stderr, "\n  Happens-before: %s\n", 
        create_happens_before_read ? "YES" : "NO");
}
```

Run and examine output.

### Phase 6: Ground Truth Verification

Manual verification:

```bash
# Create simple test
mkdir /tmp/manual_test
cd /tmp/manual_test

# In terminal 1: Create files
for i in {1..10}; do
    touch file_$i.txt
    sleep 0.1
done

# In terminal 2: Read directory
ls -la

# Question: Do we see all 10 files? (We should!)
```

Then run Xibalba on same directory:
```bash
bazel run //chaos:simple_chaos_test -- --readers 1 --writers 0 --duration 5 /tmp/manual_test
```

If it reports bugs on a **static directory** → validation is definitely broken.

## Debugging Checklist

- [ ] Check bug type distribution (all same type?)
- [ ] Verify consistent bug count across runs
- [ ] Test with zero concurrency (1 reader, 1 writer)
- [ ] Check for `lost+found` in bug reports
- [ ] Verify thread ID uniqueness
- [ ] Add debug logging for vector clocks
- [ ] Test on static directory (no concurrent writes)
- [ ] Write minimal reproduction case
- [ ] Add vector clock unit tests
- [ ] Check hash collision rate

## Most Likely Root Causes (Ranked)

1. **✅ FIXED**: `.`, `..`, and `lost+found` not filtered
   - These filesystem metadata entries were being reported as phantoms
   - Fixed in `common/dir_reader.c` by filtering these entries
   - **Result**: Reduced from 12 phantoms to 1-2 phantoms per read

2. **90% likely (CURRENT)**: Vector clock causality false positives
   - Still seeing "1 missing + 1 phantom" bugs
   - Hypothesis: Thread ID hashing causes false causality
   - Multiple threads → same VC slot → incorrect happens-before
   
3. **5% likely**: Real race conditions (actually correct bug reports!)
   - Maybe we're finding actual POSIX weak consistency edge cases
   - Need to verify with single-threaded test

4. **3% likely**: Vector clock happens_before() logic bug
   - Unit tests pass, so unlikely
   - But worth double-checking with real workload

5. **2% likely**: Real ext4 bugs
   - Would be surprising but amazing if true

## Next Steps

1. Run simplest possible test (static directory, 1 reader)
2. Check if bugs mention `lost+found`
3. Add debug logging
4. Fix most likely issue
5. Re-test

Want me to start with step 1?

