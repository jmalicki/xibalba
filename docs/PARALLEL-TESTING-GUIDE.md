# Parallel Filesystem Testing Guide

**Quick Start**: Run all filesystem comparisons in ~2 minutes

## Overview

The parallel test infrastructure runs **all filesystems × consistency models** simultaneously in separate VMs, then generates automated reports.

## Basic Usage

### Run Full Comparison

```bash
# Default: 30s duration, 4 readers, 2 writers
bazel run //tools:filesystem_comparison

# Custom duration
bazel run //tools:filesystem_comparison -- --test_env=DURATION=60

# Custom workload  
bazel run //tools:filesystem_comparison -- --test_env=DURATION=30 --test_env=READERS=8 --test_env=WRITERS=4
```

**Alternative** (with environment variables):
```bash
DURATION=60 bazel run //tools:filesystem_comparison
```

### Test Matrix

**Current Configuration**:
- Filesystems: ext4, XFS, btrfs (ZFS in progress)
- Models: posix, weak
- **Total**: 6 parallel tests

**Each test runs in its own VM**, so they don't interfere with each other.

## Understanding Results

### Natural Race Windows (Current Default)

Tests show **natural filesystem behavior** without fault injection:
- Real-world race window sizes
- Typical application behavior
- No artificial delays

**Example Results**:
```
| Filesystem | POSIX (bugs/1000) | Weak (bugs/1000) |
|------------|-------------------|------------------|
| ext4       | 0.00              | 17.31            |
| XFS        | 0.00              | 34.31            |
| btrfs      | 0.00              | 954.17           |
```

**Interpretation**:
- **POSIX**: All pass (0 duplicates = compliant) ✅
- **Weak**: ext4 strongest (1.7%), btrfs weakest (95.4%)

### Why Results Vary Between Runs

Race conditions are **timing-dependent**:
- VM scheduling
- CPU cache behavior
- Disk I/O timing
- Random thread interleaving

**Consistency across runs**:
- ✅ All filesystems remain POSIX-compliant
- ✅ Relative ordering stays same: ext4 < XFS < btrfs
- ⚠️ Absolute numbers vary (±50%)

## Output Files

After running, results are in `test-results/comparison-YYYYMMDD-HHMMSS/`:

```
comparison-20251012-055851/
├── summary.txt           # Human-readable summary
├── REPORT.md            # Markdown table (for docs)
├── ext4_posix.log       # Full ext4/posix output
├── ext4_weak.log        # Full ext4/weak output
├── xfs_posix.log        # Full XFS/posix output
├── xfs_weak.log         # Full XFS/weak output
├── btrfs_posix.log      # Full btrfs/posix output
└── btrfs_weak.log       # Full btrfs/weak output
```

### View Summary

```bash
cat test-results/comparison-*/summary.txt
```

### View Detailed Results

```bash
# See full output for a specific test
cat test-results/comparison-*/ext4_weak.log

# Extract just the bug reports
grep "Bug:" test-results/comparison-*/ext4_weak.log
```

## Adding eBPF Fault Injection

**Current Status**: Tests show natural race windows (no eBPF)

To add artificial delays and widen race windows:

### 1. Modify VM Init Script

Edit `vm/qemu/init.sh` to start `pause_controller` before running tests:

```bash
# Start eBPF fault injection (if enabled)
if [ "$EBPF_INJECT" = "1" ]; then
    echo "Starting eBPF fault injector..."
    pause_controller 10 100 1000 &  # 10% prob, 100 iterations, 1000ns max
    PAUSE_PID=$!
    sleep 1
fi

# Run tests
simple_chaos_test ...

# Cleanup
kill $PAUSE_PID 2>/dev/null || true
```

### 2. Pass eBPF Flag

Update `vm/qemu/run-qemu-test.sh` to pass `xibalba.ebpf=1`:

```bash
-append "... xibalba.ebpf=1 ..."
```

### 3. Run with eBPF Injection

```bash
# Via Bazel
EBPF_INJECT=1 bazel run //tools:filesystem_comparison

# Or pass as environment variable
bazel run //tools:filesystem_comparison -- --test_env=EBPF_INJECT=1
```

### eBPF Injection Levels

**Low (recommended for testing)**:
```bash
pause_controller 5 50 500    # 5% prob, 50 iter, 500ns
```
- Widens race windows slightly
- ~10-20% bug rate increase
- Still realistic

**Medium**:
```bash
pause_controller 10 100 1000  # 10% prob, 100 iter, 1µs
```
- Moderate widening
- ~30-50% bug rate increase
- Good for finding bugs

**High (stress test)**:
```bash
pause_controller 50 500 5000  # 50% prob, 500 iter, 5µs
```
- Extreme widening
- 80%+ bug rates
- Unrealistic but finds edge cases

## Advanced Usage

### Test Single Filesystem

```bash
bazel run //vm:qemu_test_runner -- \
    --filesystem ext4 \
    --model weak \
    --duration 60 \
    --readers 8 \
    --writers 4
```

### Compare with/without eBPF

```bash
# Natural (no eBPF)
RESULTS_DIR=results/natural bazel run //tools:filesystem_comparison

# With eBPF
RESULTS_DIR=results/ebpf EBPF_INJECT=1 bazel run //tools:filesystem_comparison

# Compare
diff results/natural/summary.txt results/ebpf/summary.txt
```

### Longer Test Runs

```bash
# 5-minute tests for more stable results
DURATION=300 bazel run //tools:filesystem_comparison
```

### More Aggressive Workload

```bash
# 16 readers, 8 writers
READERS=16 WRITERS=8 bazel run //tools:filesystem_comparison
```

## Performance

**Timing** (approximate):
- Each test: duration + 60s overhead
- Default (20s): ~80s total (parallel)
- Long (60s): ~120s total (parallel)

**Resources**:
- Each VM: 2GB RAM, 2 CPUs
- Max parallel: Limited by host resources
- Recommended: 8GB+ RAM for all 6 tests

## Troubleshooting

### Tests Timeout

Increase VM timeout in `vm/qemu/run-qemu-test.sh`:
```bash
TIMEOUT=$((DURATION + 120))  # More overhead
```

### Bazel Lock Contention

Tests may wait for Bazel lock. This is normal and handled automatically.

### VM Fails to Boot

Check logs:
```bash
cat test-results/comparison-*/ext4_posix.log | head -50
```

## CI Integration

### GitHub Actions

```yaml
- name: Run filesystem comparison
  run: bazel run //tools:filesystem_comparison
  
- name: Upload results
  uses: actions/upload-artifact@v3
  with:
    name: comparison-results
    path: test-results/comparison-*
```

## Next Steps

- [ ] Add ZFS support (requires partition node fix)
- [ ] Add eBPF injection modes
- [ ] Create HTML report generator
- [ ] Add trend tracking (compare historical results)
- [ ] Add statistical analysis (mean, variance, confidence intervals)

## See Also

- [`docs/FILESYSTEM-CONSISTENCY-COMPARISON.md`](FILESYSTEM-CONSISTENCY-COMPARISON.md) - Detailed analysis
- [`docs/ZFS-VM-LIMITATION.md`](ZFS-VM-LIMITATION.md) - ZFS partition issues
- [`docs/VM-TEST-STATUS-UPDATED.md`](VM-TEST-STATUS-UPDATED.md) - VM infrastructure status

