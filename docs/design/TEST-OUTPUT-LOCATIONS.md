# Test Output Locations: Design Decisions

## The Problem

Xibalba creates output files during test execution:
- `xibalba-bugs.jsonl` - Per-bug details
- `xibalba-progress.jsonl` - Periodic progress snapshots
- `xibalba-history.json` - Full operation history

**Issue:** If these are created IN the test directory, they appear as "phantom entries" during validation:
1. Output files are created during the test run
2. They're never tracked via [`tracker_record_create()`](../../common/state_tracker.c#L62)
3. Directory readers see them and report as phantom entries
4. Result: 2,000+ false bugs per 1,000 scans

## Options for Output Location

### Option 1: Separate Output Directory (IMPLEMENTED)

**Approach:** Create output files in a directory OUTSIDE the filesystem under test.

```bash
# Test directory: /tmp/test_dir (ext4 filesystem)
# Output directory: /tmp/test_dir.output (different directory)

xibalba-bugs.jsonl       → /tmp/test_dir.output/xibalba-bugs.jsonl
xibalba-progress.jsonl   → /tmp/test_dir.output/xibalba-progress.jsonl
xibalba-history.json     → /tmp/test_dir.output/xibalba-history.json
```

**Pros:**
- ✅ Zero interference with test directory
- ✅ Output survives even if test directory is deleted
- ✅ Simple to implement (just different path)
- ✅ Easy to find output (predictable location)

**Cons:**
- ❌ Creates clutter in parent directory

**Implementation:**
```c
// In simple_chaos_test.c:
char output_dir[PATH_MAX];
snprintf(output_dir, sizeof(output_dir), "%s.output", test_dir);
mkdir(output_dir, 0755);

char bugs_file[PATH_MAX];
snprintf(bugs_file, sizeof(bugs_file), "%s/xibalba-bugs.jsonl", output_dir);
```

**Why we chose this:**
- Simplest solution
- No risk of contaminating test filesystem
- Standard Unix pattern (`foo.output/` for output of testing `foo/`)

### Option 2: Hidden Directory Inside Test Dir

**Approach:** Create `.xibalba/` subdirectory inside test directory.

```bash
/tmp/test_dir/.xibalba/bugs.jsonl
/tmp/test_dir/.xibalba/progress.jsonl
/tmp/test_dir/.xibalba/history.json
```

**Pros:**
- ✅ Keeps output with test
- ✅ Hidden (dot-prefix convention)

**Cons:**
- ❌ Still contaminates test filesystem
- ❌ Readers might still see `.xibalba/` directory
- ❌ Need to filter `.xibalba` in dir_reader
- ❌ Could interfere with tests of dot-files

**Rejected because:** Still pollutes the test filesystem.

### Option 3: tmpfs/memfs for Output

**Approach:** Write output to in-memory filesystem.

```bash
# Output to /dev/shm (tmpfs, not disk)
xibalba --test-dir /mnt/ext4/test --output-dir /dev/shm/xibalba-$$
```

**Pros:**
- ✅ Zero disk I/O for output
- ✅ Very fast
- ✅ Completely separate from test filesystem

**Cons:**
- ❌ Output lost if system crashes
- ❌ Limited by RAM
- ❌ More complex (requires tmpfs available)

**When to use:** Performance-critical scenarios where output size is small.

### Option 4: Stream to stdout/stderr

**Approach:** Write JSONL output to stdout, let user redirect.

```bash
xibalba /tmp/test_dir > bugs.jsonl 2> progress.jsonl
```

**Pros:**
- ✅ Maximum flexibility for user
- ✅ Works with pipes, remote logging, etc.
- ✅ No filesystem pollution

**Cons:**
- ❌ Harder to use (requires shell redirection)
- ❌ Progress updates mixed with other output
- ❌ Binary output (history.json) problematic

**When to use:** CI/CD pipelines, automated testing infrastructure.

### Option 5: Write to Different Filesystem Entirely

**Approach:** User specifies output filesystem explicitly.

```bash
# Test ext4 on /mnt/test
# Write output to home directory
xibalba --test-dir /mnt/test --output-dir ~/xibalba-results/$(date +%Y%m%d-%H%M%S)
```

**Pros:**
- ✅ Total isolation
- ✅ Can use fast filesystem for output (e.g., SSD for output, HDD for test)
- ✅ Organized output collection

**Cons:**
- ❌ Requires user to specify
- ❌ More complex command line

**When to use:** Production testing, benchmarking, multi-filesystem test campaigns.

## Implementation: Option 1 (Separate Output Directory)

### Command-Line Interface

```bash
# Simple form: auto-creates test_dir.output/
xibalba --test-dir /tmp/test_dir

# Explicit form:
xibalba --test-dir /tmp/test_dir --output-dir /tmp/results/

# For VM/QEMU tests:
xibalba --test-dir /mnt/disk --output-dir /tmp/output
```

### Directory Structure

```
/tmp/test_dir/              # Test directory (clean, no output)
├── writer_12345_file_0
├── writer_12345_file_1
└── ...

/tmp/test_dir.output/       # Output directory (created automatically)
├── xibalba-bugs.jsonl      # Per-bug details (one JSON per line)
├── xibalba-progress.jsonl  # Progress snapshots (every 5 seconds)
└── xibalba-history.json    # Full operation history
```

### Code Changes

```c
// simple_chaos_test.c
void create_output_files(const char *test_dir, char *bugs_file, char *progress_file, char *history_file) {
    char output_dir[PATH_MAX];
    
    // Create output directory: test_dir.output
    snprintf(output_dir, sizeof(output_dir), "%s.output", test_dir);
    mkdir(output_dir, 0755);
    
    // Build output file paths
    snprintf(bugs_file, PATH_MAX, "%s/xibalba-bugs.jsonl", output_dir);
    snprintf(progress_file, PATH_MAX, "%s/xibalba-progress.jsonl", output_dir);
    snprintf(history_file, PATH_MAX, "%s/xibalba-history.json", output_dir);
}
```

## Future Considerations

### For CI/CD Integration
- Environment variable: `XIBALBA_OUTPUT_DIR=/ci/artifacts/run-$BUILD_ID`
- Auto-upload to S3/artifact store
- Compress output for long-running tests

### For Performance Testing
- Option to write to tmpfs (`--output-tmpfs`)
- Streaming output to remote server
- Batch writes (buffer in memory, flush periodically)

### For Debugging
- Option to write detailed trace: `--trace-file /tmp/trace.log`
- Include vector clock snapshots in output
- Per-thread operation logs

## Lessons Learned

1. **Never write test output to the filesystem under test**
   - Pollutes ground truth
   - Creates phantom entries
   - Hard to debug (are bugs real or artifacts?)

2. **Predictable output locations are better than flexible ones**
   - `test_dir.output/` is easy to remember
   - Auto-creation means less user error
   - Consistent structure helps tooling

3. **Separate output early in design**
   - Retrofitting is harder
   - Tests need to account for output
   - Documentation must be clear

## References

- [`chaos/simple_chaos_test.c`](../../chaos/simple_chaos_test.c) - Main test binary
- [`vm/qemu/init.sh`](../../vm/qemu/init.sh) - VM test wrapper (handles output redirection)
- [`docs/TESTING-GUIDE.md`](../TESTING-GUIDE.md) - User-facing documentation

