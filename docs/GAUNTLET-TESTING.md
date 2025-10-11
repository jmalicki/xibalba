# Xibalba Gauntlet - Progressive Consistency Testing

The **Xibalba Gauntlet** runs the same test parameters across all filesystems with escalating consistency models to quantitatively measure filesystem behavior and detect consistency bugs.

## Testing Philosophy

Filesystems guarantee **weak POSIX consistency** (snapshot at read start). The gauntlet tests this guarantee progressively:

1. **EVENTUAL** (baseline) → Should **always PASS**
   - Only duplicates are bugs
   - Tests basic correctness

2. **WEAK POSIX** (POSIX standard) → Should **PASS** for POSIX-compliant filesystems
   - Snapshot at read start
   - Operations during read may/may not appear
   - Tests the guaranteed behavior

3. **STRICT** (linearizable) → **Quantify departures**
   - All operations instantly visible
   - Expected to show weak consistency behavior
   - Measures how far from ideal the filesystem is

## Usage

### In VMs (Recommended)

```bash
# VM is installed with xibalba package
xibalba-gauntlet

# Custom parameters
XIBALBA_DURATION=600 xibalba-gauntlet  # 10 minutes per test
XIBALBA_READERS=20 xibalba-gauntlet    # 20 reader threads
XIBALBA_WRITERS=5 xibalba-gauntlet     # 5 writer threads
```

### Configuration

Environment variables:

- `XIBALBA_DURATION` - Seconds per test (default: 300 = 5 minutes)
- `XIBALBA_READERS` - Reader thread count (default: 10)
- `XIBALBA_WRITERS` - Writer thread count (default: 3)
- `XIBALBA_RESULTS` - Results directory (default: `/var/log/xibalba/gauntlet`)

## Test Matrix

The gauntlet tests:

**Filesystems** (5):
- ext4
- XFS
- btrfs
- ZFS
- tmpfs

**Consistency Models** (3):
- eventual
- weak
- strict

**Total tests**: 5 filesystems × 3 models = **15 tests**

## Output

### JSON Summary

Machine-readable results:

```json
{
  "timestamp": "2025-10-11T12:00:00Z",
  "duration_seconds": 300,
  "readers": 10,
  "writers": 3,
  "filesystems": ["ext4", "xfs", "btrfs", "zfs", "tmpfs"],
  "models": ["eventual", "weak", "strict"],
  "results": [
    {
      "filesystem": "ext4",
      "model": "eventual",
      "status": "PASS",
      "bugs_found": 0,
      "total_operations": 12543,
      "missing_entries": 0,
      "phantom_entries": 0,
      "duplicate_entries": 0,
      "result_file": "/var/log/xibalba/gauntlet/ext4-eventual-20251011-120000.json"
    },
    ...
  ]
}
```

### Text Summary

Human-readable summary:

```
XIBALBA GAUNTLET SUMMARY
Date: Sat Oct 11 12:00:00 UTC 2025
Duration: 300 seconds per test
Threads: 10 readers, 3 writers

═══════════════════════════════════════════════════════════════
Filesystem: ext4
───────────────────────────────────────────────────────────────
  EVENTUAL: PASS | Bugs: 0 | Ops: 12543 | Missing: 0 | Phantom: 0 | Duplicate: 0
  WEAK:     PASS | Bugs: 0 | Ops: 12487 | Missing: 0 | Phantom: 0 | Duplicate: 0
  STRICT:   FAIL | Bugs: 23 | Ops: 12501 | Missing: 15 | Phantom: 8 | Duplicate: 0

Filesystem: xfs
───────────────────────────────────────────────────────────────
  EVENTUAL: PASS | Bugs: 0 | Ops: 13245 | Missing: 0 | Phantom: 0 | Duplicate: 0
  WEAK:     PASS | Bugs: 0 | Ops: 13187 | Missing: 0 | Phantom: 0 | Duplicate: 0
  STRICT:   FAIL | Bugs: 18 | Ops: 13212 | Missing: 12 | Phantom: 6 | Duplicate: 0
...
```

## Expected Results

### Expected to PASS

- **All EVENTUAL tests** - Basic correctness (no duplicates)
- **All WEAK tests** - POSIX guarantee (snapshot consistency)

### Expected to FAIL (not bugs!)

- **STRICT tests** - Quantifies weak consistency behavior
  - Missing entries: Operations during read not yet visible
  - Phantom entries: Deleted entries still visible (delayed propagation)
  - This is **normal and expected** for weak consistency

### Actual Bugs

If these fail, it's a **real bug**:

- **EVENTUAL test fails** → Duplicate entries (serious kernel bug!)
- **WEAK test fails** → POSIX violation (filesystem bug!)

## Interpretation

### Healthy Filesystem

```
EVENTUAL: PASS ✅
WEAK:     PASS ✅
STRICT:   FAIL (missing: 15, phantom: 8) ← Expected weak consistency
```

This is **correct behavior** - filesystem implements POSIX weak consistency.

### Bug Detected

```
EVENTUAL: PASS ✅
WEAK:     FAIL (missing: 5) ❌ ← BUG!
STRICT:   FAIL
```

This indicates a **POSIX violation** - missing entries that should be visible.

### Critical Bug

```
EVENTUAL: FAIL (duplicate: 2) ❌ ← CRITICAL BUG!
```

This indicates **duplicate entries** - a serious filesystem or kernel bug.

## Performance Metrics

The gauntlet also measures:

- **Operations per second** - Throughput under concurrent load
- **Total operations** - Test coverage
- **Bug distribution** - Where weak consistency appears

Use this to:
1. **Compare filesystems** - Which handles concurrency better?
2. **Quantify weak consistency** - How "weak" is each filesystem?
3. **Detect regressions** - Did a kernel update change behavior?

## Example Run

```bash
$ xibalba-gauntlet
════════════════════════════════════════════════════════════════
    XIBALBA GAUNTLET - The Six Houses of Testing
════════════════════════════════════════════════════════════════

Test Parameters (SAME FOR ALL):
  Duration: 300 seconds (5 minutes)
  Readers: 10 threads
  Writers: 3 threads
  Filesystems: ext4 xfs btrfs zfs tmpfs
  Models: eventual weak strict

Testing Strategy:
  1. EVENTUAL → Should PASS (baseline)
  2. WEAK     → Should PASS (POSIX guarantee)
  3. STRICT   → Quantify departures (expected weak consistency)

Results: /var/log/xibalba/gauntlet
════════════════════════════════════════════════════════════════

┌─────────────────────────────────────────────────────────────┐
│ Filesystem: ext4
└─────────────────────────────────────────────────────────────┘
  Mounted at: /mnt/test

  Testing ext4 with eventual model...
    Status: PASS | Bugs: 0 | Ops: 12543 | Missing: 0 | Phantom: 0 | Duplicate: 0
  Testing ext4 with weak model...
    Status: PASS | Bugs: 0 | Ops: 12487 | Missing: 0 | Phantom: 0 | Duplicate: 0
  Testing ext4 with strict model...
    Status: FAIL | Bugs: 23 | Ops: 12501 | Missing: 15 | Phantom: 8 | Duplicate: 0

[... tests continue for all filesystems ...]

════════════════════════════════════════════════════════════════
    GAUNTLET COMPLETE
════════════════════════════════════════════════════════════════

Total tests: 15
Passed: 10
Failed: 5

Detailed results: /var/log/xibalba/gauntlet/gauntlet-summary-20251011-120000.json
```

## Integration with CI

For CI/VM testing:

```bash
# Build and deploy
bazel build //packaging:xibalba-deb
# ... deploy to VM ...

# Run gauntlet in VM
ssh vm "xibalba-gauntlet"

# Download results
scp vm:/var/log/xibalba/gauntlet/latest-gauntlet.json ./results/
```

## See Also

- `docs/design/FILESYSTEM-CONSISTENCY-MODELS.md` - Detailed consistency model explanations
- `TESTING-GUIDE.md` - General testing methodology
- `xibalba-test-runner` - Single filesystem testing

