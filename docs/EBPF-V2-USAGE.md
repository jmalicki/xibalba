# Enhanced eBPF Fault Injection (v2) - Usage Guide

## Quick Start

### Basic Usage

```bash
# Terminal 1: Start enhanced eBPF injection (requires sudo)
sudo bazel run //chaos:pause_controller_v2 -- --aggressive

# Terminal 2: Run POSIX compliance test
bazel run //chaos:simple_chaos_test -- --posix --duration 300 /tmp/test

# Watch for duplicates (the one thing POSIX forbids!)
```

**Expected Result**: 0 bugs (ext4 is POSIX-compliant even under stress!)

---

## What's New in v2?

### v1 (Original)
- ✅ Delays `sys_getdents64` only
- ✅ Widens race windows during directory READING
- ✅ Good for finding weak consistency behaviors

### v2 (Enhanced)
- ✅ Delays `sys_getdents64` (directory reading)
- ✅ Delays `vfs_create` (file creation) **NEW!**
- ✅ Delays `do_unlinkat` (file deletion) **NEW!**
- ✅ Delays `vfs_rename` (file rename) **NEW!**

**Why this matters**: Delaying directory MODIFICATIONS (not just reads) creates races during directory structure changes:
- htree splits (ext4)
- B+ tree rebalancing (XFS)
- COW updates (btrfs)

Higher probability of finding cursor confusion bugs!

---

## Command-Line Options

### Per-Syscall Delay Configuration

```bash
sudo bazel run //chaos:pause_controller_v2 -- \
  --getdents-delay 50 \    # 50% of getdents64 calls delayed
  --create-delay 30 \      # 30% of creates delayed
  --unlink-delay 30 \      # 30% of unlinks delayed
  --rename-delay 80        # 80% of renames delayed (highest risk!)
```

Each syscall can have independent delay probability (0-100%).

### Delay Amount Configuration

```bash
sudo bazel run //chaos:pause_controller_v2 -- \
  --iterations 500 \       # Busy-wait loops (controls delay duration)
  --max-delay-ns 100000    # Max 100μs delay
```

**Delay calculation**:
- `iterations`: Number of busy-wait loops (1-1000)
- `max_delay_ns`: Maximum delay in nanoseconds
- Actual delay: ~10-100μs typically

### Presets

#### Aggressive (Find ALL Races)
```bash
sudo bazel run //chaos:pause_controller_v2 -- --aggressive

# Equivalent to:
#   --getdents-delay 80
#   --create-delay 80
#   --unlink-delay 80
#   --rename-delay 90
#   --iterations 1000
#   --max-delay-ns 200000  (200μs)
```

**Use for**: Maximum stress testing, finding ANY possible bug

#### Moderate (Balanced)
```bash
sudo bazel run //chaos:pause_controller_v2 -- --moderate

# Equivalent to:
#   --getdents-delay 50
#   --create-delay 40
#   --unlink-delay 40
#   --rename-delay 70
#   --iterations 500
#   --max-delay-ns 100000  (100μs)
```

**Use for**: General testing, balanced performance vs bug detection

#### Minimal (Subtle Races)
```bash
sudo bazel run //chaos:pause_controller_v2 -- --minimal

# Equivalent to:
#   --getdents-delay 20
#   --create-delay 10
#   --unlink-delay 10
#   --rename-delay 30
#   --iterations 200
#   --max-delay-ns 50000  (50μs)
```

**Use for**: Finding only the most timing-sensitive bugs

---

## Testing Scenarios

### Scenario 1: POSIX Compliance (Baseline)

**Goal**: Verify kernel is POSIX-compliant (no duplicates)

```bash
# Terminal 1: Aggressive eBPF injection
sudo bazel run //chaos:pause_controller_v2 -- --aggressive

# Terminal 2: POSIX compliance test
bazel run //chaos:simple_chaos_test -- \
  --posix \
  --readers 10 \
  --writers 5 \
  --duration 600 \
  /tmp/posix_stress_test

# Watch for: Bugs found: 0 (should stay at 0!)
```

**Success criteria**: 0 bugs even with aggressive delays

### Scenario 2: Target Rename Races

**Goal**: Highest probability of finding cursor confusion

```bash
# Terminal 1: Focus on rename delays
sudo bazel run //chaos:pause_controller_v2 -- \
  --rename-delay 95 \
  --getdents-delay 70 \
  --create-delay 20 \
  --unlink-delay 20 \
  --iterations 800

# Terminal 2: POSIX test
bazel run //chaos:simple_chaos_test -- --posix /tmp/rename_stress
```

**Rationale**: Renames are highest risk for duplicates (entry at old + new location)

### Scenario 3: Directory Growth (htree/B+tree Splits)

**Goal**: Trigger directory rebalancing during scans

```bash
# Terminal 1: Focus on create delays
sudo bazel run //chaos:pause_controller_v2 -- \
  --create-delay 80 \
  --getdents-delay 70 \
  --iterations 1000

# Terminal 2: Many writers to trigger directory growth
bazel run //chaos:simple_chaos_test -- \
  --posix \
  --readers 5 \
  --writers 20 \  # Many writers!
  --duration 600 \
  /tmp/growth_stress
```

**Rationale**: Rapid directory growth triggers htree/B+tree splits

### Scenario 4: Weak Consistency Research

**Goal**: Measure how eBPF affects race window sizes

```bash
# Baseline (no eBPF):
bazel run //chaos:simple_chaos_test -- --weak /tmp/baseline
# Result: ~4-5 bugs/1000 scans

# With v2 eBPF:
sudo bazel run //chaos:pause_controller_v2 -- --moderate
bazel run //chaos:simple_chaos_test -- --weak /tmp/with_ebpf
# Expected: ~10-50 bugs/1000 scans

# Measure: How much did eBPF widen the race windows?
```

---

## Understanding the Statistics

### Live Stats (Every 5 Seconds)

```
Stats: getdents: 12453/24901 (50.0%)  create: 1234/4112 (30.0%)  unlink: 823/2741 (30.0%)  rename: 45/56 (80.4%)
```

**Interpretation**:
- `getdents: 12453/24901 (50.0%)` - 50% of getdents64 calls were delayed (as configured)
- `rename: 45/56 (80.4%)` - 80% of renames delayed (high risk!)

### Final Statistics

```
────────────────────────────────────────────────────────────
  Syscall      │   Total Calls │     Delayed │   Delay %
────────────────────────────────────────────────────────────
  getdents64   │       248901 │      124453 │     50.0%
  create       │        41123 │       12337 │     30.0%
  unlink       │        27412 │        8224 │     30.0%
  rename       │          562 │          451 │     80.2%
────────────────────────────────────────────────────────────
  Total delays injected: 145465
  Est. total delay time: 14546.50 ms
```

**What this tells you**:
- How many syscalls were intercepted
- How many were actually delayed
- Total time spent in artificial delays

---

## Interpreting Results

### If You Find 0 Bugs with --posix + v2 eBPF

**Verdict**: Kernel is extremely robust!

**Meaning**:
- No duplicates even under stress
- Cursor management is solid
- Directory rebalancing works correctly
- POSIX-compliant under all conditions tested

**This is GOOD NEWS!** (Not a failure)

### If You Find Duplicates with --posix + v2 eBPF

**Verdict**: Potential kernel bug! 🚨

**Next Steps**:
1. **Verify reproducibility**:
   ```bash
   # Run 10 times, see if bug is consistent
   for i in {1..10}; do
     ./run_posix_test.sh | grep "Bugs found"
   done
   ```

2. **Create minimal reproducer**:
   - Reduce duration, threads, operations
   - Find minimum configuration that triggers bug

3. **Check kernel version**:
   ```bash
   uname -r
   cat /proc/version
   ```

4. **Test other filesystems**:
   ```bash
   # Is it ext4-specific or all filesystems?
   mkfs.xfs /dev/loop0 && mount /dev/loop0 /mnt/xfs
   bazel run //chaos:simple_chaos_test -- --posix /mnt/xfs/test
   ```

5. **Report to kernel team**:
   - Full reproducer with eBPF config
   - Kernel version, filesystem type
   - Xibalba bug output (JSONL)

---

## Performance Impact

### Without eBPF v2

- Throughput: ~11,000 ops/second
- Latency: Normal (microseconds)

### With eBPF v2 (Moderate Preset)

- Throughput: ~3,000-5,000 ops/second (2-3x slower)
- Latency: +50-100μs per delayed operation
- Total added delay: ~10-20 seconds over 5 minutes

**Trade-off**: 2-3x slower, but much higher bug detection probability

### With eBPF v2 (Aggressive Preset)

- Throughput: ~1,000-2,000 ops/second (5-10x slower)
- Latency: +100-200μs per delayed operation
- Total added delay: ~30-60 seconds over 5 minutes

**Trade-off**: Much slower, but maximum stress

---

## Comparison: v1 vs v2

| Feature | v1 (Original) | v2 (Enhanced) |
|---------|---------------|---------------|
| Syscalls hooked | 1 (getdents64) | 4 (getdents, create, unlink, rename) |
| Configuration | Single delay % | Per-syscall delay % |
| Rename support | No | Yes (highest priority!) |
| Statistics | Basic | Per-syscall breakdown |
| Presets | No | Yes (aggressive, moderate, minimal) |
| Use case | Weak consistency | POSIX violation hunting |

---

## Expected Bug Rates

| Model | No eBPF | v1 eBPF | v2 eBPF (Aggressive) |
|-------|---------|---------|----------------------|
| `--posix` | 0 | 0 (expected) | **0 (expected)** |
| `--weak` | 4-5/1000 | 10-20/1000 | **20-50/1000** |
| `--strict` | 4-5/1000 | 10-20/1000 | **20-50/1000** |

**Key point**: Even v2 with aggressive delays probably won't find POSIX violations on production kernels. But it's worth trying!

---

## When to Use v1 vs v2

### Use v1 (pause_controller) When:
- Testing weak consistency (--weak model)
- Simple delay injection
- Lower performance impact
- Production-like testing

### Use v2 (pause_controller_v2) When:
- Hunting for POSIX violations (--posix model)
- Want maximum stress (--aggressive)
- Research: measuring race window effects
- Testing directory structure changes (htree splits, etc.)

---

## Troubleshooting

### eBPF Program Won't Load

```
Failed to load eBPF object: Operation not permitted
```

**Fix**: Grant CAP_BPF capability:
```bash
sudo ./grant_caps.sh
# Or run with sudo
sudo bazel run //chaos:pause_controller_v2 -- --moderate
```

### No Statistics Showing

```
Stats: getdents: 0/0  create: 0/0  unlink: 0/0  rename: 0/0
```

**Causes**:
1. No workload running (start simple_chaos_test in another terminal)
2. eBPF hooks not attached (check dmesg for errors)
3. Wrong syscall names (kernel version mismatch)

### High CPU Usage

```
CPU at 100% during eBPF injection
```

**This is normal!** Busy-wait delays consume CPU by design.

To reduce:
- Lower `--iterations`
- Lower delay percentages
- Use `--minimal` preset

---

## Safety

### Is This Safe on Production Systems?

**Short answer**: Generally yes, but use caution.

**Details**:
- ✅ No data corruption (only delays, no modifications)
- ✅ No crashes (well-tested eBPF hooks)
- ⚠️ Performance impact (2-10x slower)
- ⚠️ High CPU usage (busy-wait delays)
- ⚠️ Affects ALL processes (system-wide)

**Recommendation**: Use on test systems, not production!

### How to Stop

```
Press Ctrl+C in pause_controller_v2 terminal
```

eBPF programs are automatically detached, delays stop immediately.

---

## Research Questions This Helps Answer

1. **How robust are directory operations under extreme stress?**
   - Answer: Run v2 aggressive + POSIX test
   
2. **What's the relationship between delay amount and bug rate?**
   - Answer: Test with different --iterations values

3. **Which syscalls are most important for race detection?**
   - Answer: Test with each syscall disabled

4. **Can we trigger duplicates on ANY filesystem?**
   - Answer: Test ext4, XFS, btrfs, tmpfs with v2

---

## Advanced: Targeted Testing

### Test Only Renames (Highest Risk)

```bash
sudo bazel run //chaos:pause_controller_v2 -- \
  --getdents-delay 50 \
  --create-delay 0 \
  --unlink-delay 0 \
  --rename-delay 95 \
  --iterations 1000
```

### Test Only Directory Growth

```bash
sudo bazel run //chaos:pause_controller_v2 -- \
  --getdents-delay 60 \
  --create-delay 90 \
  --unlink-delay 10 \
  --rename-delay 0
```

### Minimal Impact (Production-Safe)

```bash
sudo bazel run //chaos:pause_controller_v2 -- \
  --getdents-delay 10 \
  --create-delay 5 \
  --unlink-delay 5 \
  --rename-delay 15 \
  --iterations 100
```

---

## Example Research Session

```bash
# Experiment: Find the minimum delay to trigger bugs in --weak model

# Baseline (no eBPF)
bazel run //chaos:simple_chaos_test -- --weak /tmp/baseline
# Result: 4.39 bugs/1000 scans

# Test 1: Minimal delays
sudo bazel run //chaos:pause_controller_v2 -- --minimal
bazel run //chaos:simple_chaos_test -- --weak /tmp/test1
# Result: ? bugs/1000 scans

# Test 2: Moderate delays
sudo bazel run //chaos:pause_controller_v2 -- --moderate
bazel run //chaos:simple_chaos_test -- --weak /tmp/test2
# Result: ? bugs/1000 scans

# Test 3: Aggressive delays
sudo bazel run //chaos:pause_controller_v2 -- --aggressive
bazel run //chaos:simple_chaos_test -- --weak /tmp/test3
# Result: ? bugs/1000 scans

# Plot: Bug rate vs delay amount
# Conclusion: How sensitive are race windows to timing?
```

---

## References

- [`chaos/pause_injector_v2.bpf.c`](../chaos/pause_injector_v2.bpf.c) - eBPF program
- [`chaos/pause_controller_v2.c`](../chaos/pause_controller_v2.c) - Controller
- [`docs/design/EBPF-FAULT-INJECTION-FOR-POSIX-BUGS.md`](design/EBPF-FAULT-INJECTION-FOR-POSIX-BUGS.md) - Design rationale
- [`docs/TESTING-GUIDE.md`](TESTING-GUIDE.md) - General testing guide

---

**Remember**: Finding 0 bugs with aggressive eBPF is GOOD NEWS! It means the kernel is robust. The goal is validation, not necessarily finding bugs.

