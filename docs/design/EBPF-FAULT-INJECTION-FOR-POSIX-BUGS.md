# eBPF Fault Injection for Finding POSIX Violations

## The Challenge

**POSIX forbids only ONE thing for readdir():**
> "The same file shall not be returned twice within a single traversal."

**Current Results (WITHOUT eBPF):**
- `--posix` model: **0 duplicates** in 300,000+ scans
- ext4 is POSIX-compliant ✅

**Question**: Can eBPF fault injection find POSIX violations (duplicates) that don't occur naturally?

---

## What Could Cause Duplicates? (Theoretical)

### 1. Cursor Management Bugs

**Scenario**: Directory iterator cursor gets confused

```
Directory: [A, B, C, D]
Reader position: at B

Writer: Insert X between B and C
→ [A, B, X, C, D]

Reader continues: might re-read B or C
→ DUPLICATE!
```

**ext4 specific**: Hash tree cursor management during concurrent htree split

### 2. Hash Tree Rebalancing (ext4)

**ext4 uses htree** (hash tree) for large directories:

```
Before split:
  Block 0: [files hashing to 0x00-0xFF]

During split (reader at position 0x80):
  Block 0: [0x00-0x7F]
  Block 1: [0x80-0xFF]  ← Reader cursor moves here
  
If cursor tracking is buggy: might re-read some entries
```

**eBPF opportunity**: Delay during htree split to increase race probability

### 3. B+ Tree Rebalancing (XFS, btrfs)

**B+ tree split during scan**:

```
Leaf node: [A, B, C, D, E]  (reader at C)

Split happens:
  Left:  [A, B]
  Right: [C, D, E]  ← Cursor should move here

If cursor reset: might re-read A, B
→ DUPLICATE!
```

### 4. Rename Race (Rare)

**Rename during scan**:

```
Directory /tmp/test: [A, B]
Reader: Already read A

rename("B", "C")  ← Changes entry

Reader continues: Sees C (was B)
If rename inserted entry: might see B again at old position
→ DUPLICATE!
```

### 5. Directory Dentry Cache Corruption

**Linux dcache** (directory entry cache):

```
dcache: [A → inode 123, B → inode 456]

Concurrent modification + cache invalidation race:
  - Reader uses cached entry for B
  - Writer modifies directory
  - Cache partially invalidates
  - Reader re-fetches, gets B again
→ DUPLICATE!
```

---

## Current eBPF Injection (What We Do)

### Traced Syscall: getdents64

```c
SEC("kprobe/sys_getdents64")
int trace_getdents64(struct pt_regs *ctx) {
    // Inject delay with configured probability
    if (should_inject_delay()) {
        busy_wait(delay_iterations);  // ~10-500μs delay
    }
    return 0;
}
```

**Effect**: Widens race window DURING directory reading

**What it helps find**:
- Concurrent modifications during scan
- Timing-sensitive races
- Cache invalidation races

**What it DOESN'T help find**:
- Bugs that require specific syscall interleaving
- Bugs during directory modification (not getdents)
- Bugs in cursor management between getdents calls

---

## Enhanced eBPF Injection Strategies

### Strategy 1: Multi-Point Delays (RECOMMENDED)

**Trace multiple syscalls**:

```c
// Current: delay during readdir
SEC("kprobe/sys_getdents64")
int trace_getdents64() { delay(); }

// NEW: delay during directory modifications
SEC("kprobe/do_unlinkat")
int trace_unlink() { delay(); }

SEC("kprobe/do_mkdirat")
int trace_mkdir() { delay(); }

SEC("kprobe/vfs_create")
int trace_create() { delay(); }

SEC("kprobe/vfs_rename")
int trace_rename() { delay(); }
```

**Benefit**: Creates races DURING directory modification operations

**Example race this could expose**:
```
Thread A: getdents64 (delayed by eBPF)
Thread B: unlink (ALSO delayed by eBPF)
→ Both stuck, then both complete together
→ Cursor might get confused
→ DUPLICATE possible!
```

### Strategy 2: Targeted Directory Structure Operations

**Trace filesystem-specific internals**:

```c
// ext4: Trace htree operations
SEC("kprobe/ext4_htree_fill_tree")
int trace_htree_fill() { delay(); }

SEC("kprobe/ext4_dx_add_entry")  // htree entry addition
int trace_htree_add() { delay(); }

// XFS: Trace B+ tree operations
SEC("kprobe/xfs_dir2_leafn_lookup_int")
int trace_xfs_lookup() { delay(); }

// btrfs: Trace COW operations
SEC("kprobe/btrfs_search_slot")
int trace_btrfs_search() { delay(); }
```

**Benefit**: Targets the exact code paths where cursor bugs could occur

**Risk**: Very filesystem-specific, may need separate eBPF programs per FS

### Strategy 3: Delay Between getdents64 Calls

**Current**: Single delay during getdents64

**Enhanced**: Multiple delays within one readdir scan

```c
// Track scan state
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, u64);      // pid_tid
    __type(value, u32);    // scan_count
} scan_state SEC(".maps");

SEC("kprobe/sys_getdents64")
int trace_getdents64(struct pt_regs *ctx) {
    u64 pid_tid = bpf_get_current_pid_tgid();
    u32 *count = bpf_map_lookup_elem(&scan_state, &pid_tid);
    
    if (count) {
        (*count)++;
        // Delay on 2nd, 4th, 6th call in same scan
        if (*count % 2 == 0) {
            delay();
        }
    } else {
        u32 initial = 1;
        bpf_map_update_elem(&scan_state, &pid_tid, &initial, BPF_ANY);
    }
}
```

**Benefit**: Creates pauses BETWEEN directory reads, letting modifications happen mid-scan

### Strategy 4: Conditional Delays (Smart Injection)

**Delay only when specific conditions met**:

```c
SEC("kprobe/sys_getdents64")
int trace_getdents64(struct pt_regs *ctx) {
    // Only delay if directory is "large" (>N entries)
    // This targets htree/B-tree rebalancing scenarios
    
    if (directory_size > threshold) {
        delay();
    }
}
```

**Benefit**: Targets specific bug scenarios (tree splits, rebalancing)

### Strategy 5: Delay + Memory Barrier Breaking

**Combine delay with memory ordering**:

```c
SEC("kprobe/sys_getdents64")
int trace_getdents64(struct pt_regs *ctx) {
    delay();
    
    // Force memory barrier violation (x86 specific)
    // Note: May not be possible in eBPF, needs research
    asm volatile("mfence" ::: "memory");
}
```

**Benefit**: Could expose memory ordering bugs

**Risk**: May not be possible in eBPF sandbox

---

## Specific Bugs Each Strategy Could Find

### Duplicates from Cursor Confusion

**Caused by**: Directory rebalancing during scan

**eBPF strategy**: Delay getdents64 + delay create/unlink
- Thread A: getdents64 (cursor at position N)
- eBPF: Delay!
- Thread B: Creates many files → triggers htree split
- Thread A: Resumes with stale cursor position
- Result: Cursor confusion → **DUPLICATE**

**How to trigger**:
```bash
# High probability delays on BOTH getdents64 and create
pause_controller --delay-probability-pct 80 --delay-iterations 1000

# Many writers to trigger directory growth
simple_chaos_test --writers 10 --posix /tmp/test
```

### Duplicates from Hash Collisions

**ext4 htree uses hash(filename) % buckets**

If two files hash to same value during rebalancing:

```
File A: hash = 0x42
File B: hash = 0x42  (collision!)

During htree split, both might get inserted twice
→ DUPLICATE in readdir scan
```

**eBPF strategy**: Delay during hash insertion
- Trace `ext4_dx_add_entry` (hash table insertion)
- Delay with high probability
- Increases chance of collision race

### Duplicates from RCU Grace Periods

**Linux uses RCU** for directory entry cache:

```
Writer: Update dentry (RCU pointer swap)
Reader: In RCU read-side critical section

If delay inserted at wrong point:
  - Reader might see old pointer AND new pointer
  - Could read same entry from both
→ DUPLICATE!
```

**eBPF strategy**: Delay to extend RCU grace period
- Delay reader during RCU critical section
- Might expose RCU bugs (very unlikely on production kernel)

---

## Recommended Enhanced eBPF Program

### Multi-Syscall Injection

```c
// pause_injector_v2.bpf.c

// Configuration
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, u32);
    __type(value, u64);
} config SEC(".maps");

#define CFG_GETDENTS_DELAY_PCT    0
#define CFG_CREATE_DELAY_PCT      1
#define CFG_UNLINK_DELAY_PCT      2
#define CFG_RENAME_DELAY_PCT      3
#define CFG_DELAY_ITERATIONS      4
#define CFG_MAX_DELAY_NS          5

// Delay getdents64 (current)
SEC("kprobe/sys_getdents64")
int trace_getdents64(struct pt_regs *ctx) {
    if (should_delay(CFG_GETDENTS_DELAY_PCT)) {
        inject_delay(CFG_DELAY_ITERATIONS, CFG_MAX_DELAY_NS);
    }
    return 0;
}

// NEW: Delay during file creation
SEC("kprobe/vfs_create")
int trace_create(struct pt_regs *ctx) {
    if (should_delay(CFG_CREATE_DELAY_PCT)) {
        inject_delay(CFG_DELAY_ITERATIONS, CFG_MAX_DELAY_NS);
    }
    return 0;
}

// NEW: Delay during unlink
SEC("kprobe/do_unlinkat")
int trace_unlink(struct pt_regs *ctx) {
    if (should_delay(CFG_UNLINK_DELAY_PCT)) {
        inject_delay(CFG_DELAY_ITERATIONS, CFG_MAX_DELAY_NS);
    }
    return 0;
}

// NEW: Delay during rename (high-risk for duplicates!)
SEC("kprobe/vfs_rename")
int trace_rename(struct pt_regs *ctx) {
    if (should_delay(CFG_RENAME_DELAY_PCT)) {
        inject_delay(CFG_DELAY_ITERATIONS, CFG_MAX_DELAY_NS);
    }
    return 0;
}
```

**Usage**:
```bash
# Delay all operations to create complex race scenarios
pause_controller \
  --getdents-delay 50 \
  --create-delay 30 \
  --unlink-delay 30 \
  --rename-delay 80 \  # Rename is highest risk!
  --iterations 500
```

---

## Filesystem-Specific Strategies

### For ext4: Target Htree Operations

**High-value eBPF hooks**:

```c
SEC("kprobe/ext4_htree_fill_tree")      // Reading htree
SEC("kprobe/ext4_dx_add_entry")         // Adding to htree
SEC("kprobe/ext4_htree_split")          // Htree split (HIGH RISK!)
SEC("kprobe/ext4_delete_entry")         // Deleting from htree
```

**Why htree split is high-risk**:
- Changes directory structure mid-scan
- Cursor must be updated
- Classic source of cursor confusion bugs

**Test strategy**:
```bash
# Create many files to trigger htree split
# Delay during split to widen race window
simple_chaos_test --writers 20 --posix /tmp/test
```

### For XFS: Target B+ Tree Operations

```c
SEC("kprobe/xfs_dir2_leafn_lookup_int")  // B+ tree lookup
SEC("kprobe/xfs_dir2_block_to_leaf")     // Block→leaf conversion
SEC("kprobe/xfs_dir2_leaf_to_node")      // Leaf→node conversion  
SEC("kprobe/xfs_dir2_leafn_split")       // B+ tree split
```

### For btrfs: Target COW Operations

```c
SEC("kprobe/btrfs_search_slot")          // Tree search
SEC("kprobe/btrfs_split_item")           // COW split
SEC("kprobe/btrfs_del_items")            // COW delete
```

---

## Would Any of This Find POSIX Bugs?

### Honest Assessment

**Likelihood of finding duplicates on production ext4**: **Very Low** (~0.001%)

**Why**:
1. ext4 has been tested by millions of systems for 15+ years
2. Cursor management code is well-tested
3. htree split logic is mature
4. Race conditions in this area would cause widespread issues

### But Worth Testing Because:

1. **Validates eBPF works** - Confirms injection increases some bug rate
2. **Exercises code paths** - Tests htree split, B+ tree rebalance
3. **Documentation value** - Shows what POSIX actually requires
4. **Research** - Measures timing sensitivity of directory operations

### Expected Results WITH eBPF

| Model | Without eBPF | With eBPF (50% @ 500 iter) | What Changed |
|-------|--------------|----------------------------|--------------|
| `--posix` | 0 bugs | **0-1 bugs?** | Duplicates (if any exist) |
| `--weak` | 4-5/1000 | **10-50/1000** | More missing/phantom |
| `--strict` | 4-5/1000 | **10-50/1000** | More missing/phantom |

**Prediction**: Even with eBPF, `--posix` will show 0 bugs on stable kernels.

---

## Alternative: Stress Test Scenarios

### Scenario 1: Massive Directory Growth

**Force htree/B+ tree rebalancing**:

```bash
# Create 100,000 files rapidly to trigger many splits
simple_chaos_test \
  --writers 50 \
  --readers 10 \
  --duration 600 \
  --posix \
  /tmp/large_dir_test
```

**With eBPF delay during splits**: Might expose cursor bugs

### Scenario 2: Rename Storm

**Force rename races**:

```bash
# Modified test: writers do renames instead of create/delete
# This is HIGH RISK for cursor confusion

# Would need to modify simple_chaos_test to support rename operations
```

**Benefit**: Renames change directory structure while maintaining entry count

### Scenario 3: Concurrent Rewinddir

**Test cursor reset**:

```c
DIR *d = opendir("/tmp/test");
readdir(d);  // Read some entries
rewinddir(d);  // Reset cursor
readdir(d);  // Read again

// Should NOT see duplicates even with concurrent modifications
```

**eBPF**: Delay between readdir calls

---

## Recommended Approach

### Phase 1: Current Testing (Baseline)

```bash
# Test WITHOUT eBPF
simple_chaos_test --posix --duration 300 /tmp/test

# Expected: 0 bugs (validates ext4 POSIX compliance)
```

### Phase 2: Basic eBPF (Validation)

```bash
# Enable current eBPF (getdents64 delays only)
pause_controller --delay-probability-pct 50 --delay-iterations 500

# Test with delays
simple_chaos_test --posix --duration 300 /tmp/test

# Expected: Still 0 bugs (but worth confirming!)
```

### Phase 3: Multi-Point eBPF (If Bugs Found in Phase 2)

```bash
# Enhanced eBPF with create/unlink/rename delays
# (Would need to implement multi-syscall eBPF program)

pause_controller_v2 \
  --getdents-delay 50 \
  --create-delay 30 \
  --unlink-delay 30 \
  --rename-delay 80

simple_chaos_test --posix --duration 600 /tmp/test
```

### Phase 4: Filesystem-Specific (Research)

```bash
# ext4-specific eBPF hooks
ext4_pause_controller --htree-split-delay 90

# XFS-specific
xfs_pause_controller --btree-split-delay 90
```

---

## What Would Actual POSIX Bugs Look Like?

### Example Bug Report (Hypothetical)

```json
{
  "model": "posix",
  "duplicates": 1,
  "files_read": 142,
  "duplicate_file": "writer_12345_file_42",
  "positions": [37, 89],
  "filesystem": "ext4",
  "kernel": "6.14.0-33-generic",
  "ebpf_config": {
    "getdents_delay": "50%",
    "create_delay": "30%",
    "iterations": 500
  },
  "directory_size_at_bug": 143,
  "htree_level": 2
}
```

**Significance**: This would be a **real kernel bug!**

### How to Verify

1. **Reproduce** - Run test 100 times
2. **Bisect kernel** - Find which kernel version introduced it
3. **Report to kernel team** - With full reproducer
4. **Add regression test** - Keep testing to prevent reoccurrence

---

## Implementation Priorities

### Must Have (Phase 1)
- ✅ Current getdents64 delay (implemented)
- ✅ POSIX compliance model (implemented)
- ⬜ Test with eBPF enabled to confirm 0 bugs

### Should Have (Phase 2)
- ⬜ Multi-syscall delay (create, unlink, rename)
- ⬜ Per-operation delay configuration
- ⬜ Statistics on which delays triggered

### Nice to Have (Phase 3)
- ⬜ Filesystem-specific hooks (ext4 htree, XFS B+tree)
- ⬜ Adaptive delay (increase delay if no bugs found)
- ⬜ Delay patterns (delay pairs of operations)

### Research (Phase 4)
- ⬜ Memory barrier injection
- ⬜ CPU affinity manipulation
- ⬜ Cache flush injection

---

## Expected Outcomes

### Realistic Expectation

**On production Linux (6.x kernel) with ext4/XFS/btrfs**:
- `--posix` model: **0 bugs** (even with aggressive eBPF)
- `--weak` model: **10-50 bugs/1000** (with eBPF, vs 4-5 without)
- `--strict` model: **Similar to weak**

### If We Find a POSIX Bug (duplicate)

**Probability**: < 0.1% (very unlikely)

**Actions**:
1. Celebrate! (Found a real kernel bug!)
2. Create minimal reproducer
3. Report to kernel team with full details
4. Add to regression test suite
5. Test other filesystems (is it ext4-specific?)

---

## Why This Matters Even If We Find 0 Bugs

1. **Validates framework** - Proves our tool works correctly
2. **Documents POSIX** - Deep understanding of what's actually guaranteed
3. **Research contribution** - Empirical data on directory consistency
4. **Future-proofing** - Will catch regressions in new kernels
5. **Education** - Teaches what POSIX actually means

---

## Conclusion

**Current eBPF** (getdents64 delays):
- Good for finding weak consistency behaviors (4→10 bugs/1000)
- Unlikely to find POSIX violations (duplicates)

**Enhanced eBPF** (multi-syscall, filesystem-specific):
- Better chance of finding duplicates (still very low)
- More comprehensive stress testing
- Research value

**Recommendation**: 
1. Test current setup with eBPF enabled (`--posix` should still show 0 bugs)
2. If 0 bugs confirmed: Document that ext4 is rock-solid!
3. Implement multi-syscall eBPF for research/completeness
4. Use `--weak` model with eBPF for finding interesting races (not POSIX bugs)

**Bottom Line**: eBPF is great for widening race windows in the `--weak` model, but finding actual POSIX violations (duplicates) on production kernels is extremely unlikely - which is a GOOD thing!

