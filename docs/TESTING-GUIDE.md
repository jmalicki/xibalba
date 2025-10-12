# Xibalba Testing Guide

Complete guide to running Xibalba chaos tests with current accurate commands.

## Quick Start

### 1. Build Everything

```bash
cd /path/to/xibalba
bazel build //chaos:all //vm:all
```

### 2. Run Unit Tests (Fast!)

```bash
# Core validation logic tests (15 tests, ~0.3 seconds)
bazel test //common:state_tracker_test

# These tests verify:
# - Bug detection works (missing, duplicates, phantoms)
# - No false positives
# - Vector clock causality tracking
# - Thread safety
```

### 3. Run Simple Chaos Test (Local Filesystem)

```bash
# Create test directory
mkdir -p /tmp/xibalba_test

# Run chaos test (default: 5 minutes, 10 readers, 3 writers)
bazel run //chaos:simple_chaos_test -- /tmp/xibalba_test

# Custom parameters:
bazel run //chaos:simple_chaos_test -- \
    --duration 30 \
    --readers 5 \
    --writers 2 \
    /tmp/xibalba_test

# With strict consistency model:
bazel run //chaos:simple_chaos_test -- --strict /tmp/xibalba_test
```

**Output files** (in `/tmp/xibalba_test/`):
- `xibalba-progress.jsonl` - One JSON line per 5 seconds
- `xibalba-bugs.jsonl` - One JSON line per bug event  
- `xibalba-history.json` - Complete operation history

### 4. Run with eBPF Delay Injection

**Terminal 1 - Start delay injector**:
```bash
# Build and grant capabilities (one-time)
bazel build //chaos:pause_controller
sudo setcap cap_sys_admin,cap_bpf,cap_perfmon+ep bazel-bin/chaos/pause_controller

# Run delay injector
# Usage: pause_controller <probability%> <iterations> [max_delay_ns]
bazel run //chaos:pause_controller -- 50 500

# Examples:
# Mild delays (5μs):
bazel run //chaos:pause_controller -- 30 300

# Aggressive delays (10-50μs):  
bazel run //chaos:pause_controller -- 100 1000 100000
```

**Terminal 2 - Run test**:
```bash
mkdir -p /tmp/xibalba_test
bazel run //chaos:simple_chaos_test -- --duration 60 /tmp/xibalba_test
```

**What happens**:
- eBPF intercepts 50% of getdents64 syscalls
- Injects 5-10μs delays (configurable)
- Widens race windows 10,000x!
- Bugs that appear once per million ops → appear every few seconds

### 5. Run VM Tests (Hermetic QEMU)

```bash
# Single filesystem test (ext4, 10 seconds)
bazel run //vm:qemu_test_runner -- --filesystem ext4 --duration 10 --readers 5 --writers 2

# Quick test with defaults (ext4, 300sec, 10 readers, 3 writers)
bazel run //vm:qemu_test_runner

# Run full test suite (ext4, xfs, btrfs in parallel)
bazel test //vm:qemu_filesystem_suite

# Individual filesystem tests:
bazel test //vm:qemu_test_ext4
bazel test //vm:qemu_test_xfs
bazel test //vm:qemu_test_btrfs
```

**VM test parameters** (all optional with sensible defaults):
```bash
bazel run //vm:qemu_test_runner -- [OPTIONS]

Options:
  --filesystem FS   Filesystem to test (ext4, xfs, btrfs) [default: ext4]
  --duration SEC    Test duration in seconds [default: 300]
  --readers N       Number of reader threads [default: 10]
  --writers N       Number of writer threads [default: 3]

Examples:
  bazel run //vm:qemu_test_runner -- --filesystem ext4 --duration 30
  bazel run //vm:qemu_test_runner -- --filesystem xfs --readers 20 --writers 5
  bazel run //vm:qemu_test_runner -- --duration 60  # Uses default ext4
```

## Analyzing Results

### Progress Analysis

```bash
# Analyze time-series data
tools/analyze-progress.sh /tmp/xibalba_test/xibalba-progress.jsonl

# Output shows:
# - Operations per 5-second period
# - Bug rate over time
# - Performance metrics
```

### Bug Analysis

```bash
# View detailed bug events
cat /tmp/xibalba_test/xibalba-bugs.jsonl | jq '.'

# Count bugs by type:
cat xibalba-bugs.jsonl | jq -r '.missing' | grep -c "^[1-9]"  # Missing
cat xibalba-bugs.jsonl | jq -r '.phantoms' | grep -c "^[1-9]"  # Phantoms
cat xibalba-bugs.jsonl | jq -r '.duplicates' | grep -c "^[1-9]"  # Duplicates

# Find bugs at specific times:
cat xibalba-bugs.jsonl | jq 'select(.ts > 10000000000)'  # After 10 seconds
```

### Full History

```bash
# Complete operation log (can be large!)
cat /tmp/xibalba_test/xibalba-history.json | jq '.operations | length'

# Find specific operations:
cat xibalba-history.json | jq '.operations[] | select(.type == "CREATE")'
```

## Understanding Output

### Progress JSONL Format

```json
{"elapsed":5,"ops":106047,"reads":104745,"bugs":101,"period_ops":106047,"period_reads":104745,"period_bugs":101,"period_bug_rate":0.000964,"ops_per_sec":21209.40}
```

**Fields**:
- `elapsed`: Seconds elapsed
- `ops`: Cumulative total operations  
- `reads`: Cumulative directory scans
- `bugs`: Cumulative bugs found
- `period_*`: Stats for this 5-second period only
- `period_bug_rate`: Bugs per read in this period
- `ops_per_sec`: Throughput in this period

### Bug JSONL Format

```json
{"ts":1376141992,"thread":126007229598400,"read_start":1376008433,"read_end":1376141992,"model":"weak","total_bugs":1,"missing":1,"duplicates":0,"phantoms":0,"files_read":9}
```

**Fields**:
- `ts`: Timestamp when bug found (nanoseconds)
- `thread`: Which thread found it
- `read_start/end`: Read operation timing
- `model`: Consistency model ("strict", "weak", "eventual")
- `total_bugs`: Bugs in this read operation
- `missing`: Files that should exist but don't
- `duplicates`: Files appearing multiple times
- `phantoms`: Files that shouldn't exist
- `files_read`: Total files in this scan

## Consistency Models

### Strict (Linearizable)

```bash
bazel run //chaos:simple_chaos_test -- --strict /tmp/test
```

**Guarantees**:
- If create happens-before read → file MUST be visible
- Strictest model, finds most bugs
- Use for: Critical data, databases

### Weak POSIX (Default)

```bash
bazel run //chaos:simple_chaos_test -- --weak /tmp/test
# OR just:
bazel run //chaos:simple_chaos_test -- /tmp/test
```

**Guarantees**:
- If create happens-before read start → file MUST be visible
- Concurrent operations: either outcome valid
- Use for: Most filesystem code

### Eventual

```bash
bazel run //chaos:simple_chaos_test -- --eventual /tmp/test
```

**Guarantees**:
- Only duplicates are bugs
- Missing/phantom entries allowed (propagation delays)
- Use for: Eventually consistent systems

## CI/CD Integration

### GitHub Actions (Automatic)

Tests run on every PR:

```yaml
# Unit tests (fast, every PR)
- bazel test //common:state_tracker_test

# Build verification
- bazel build //chaos:all //vm:all

# VM tests (on-demand, label PR with 'run-vm-tests')
- bazel test //vm:qemu_filesystem_suite
```

### Local CI Simulation

```bash
# Run what CI runs:
bazel test //common:state_tracker_test  # Unit tests
bazel build //chaos:all                  # Build verification
bazel test //vm:qemu_filesystem_suite   # VM tests (requires KVM)
```

## Troubleshooting

### eBPF Permission Denied

```bash
# Grant capabilities (one-time):
sudo setcap cap_sys_admin,cap_bpf,cap_perfmon+ep bazel-bin/chaos/pause_controller

# Verify:
getcap bazel-bin/chaos/pause_controller
```

### KVM Not Available

```bash
# Check KVM support:
ls -la /dev/kvm

# If missing:
sudo modprobe kvm kvm_intel  # or kvm_amd
sudo chmod 666 /dev/kvm
```

### Tests Timeout

```bash
# Reduce test duration:
bazel run //chaos:simple_chaos_test -- --duration 10 /tmp/test

# Or increase timeout:
bazel test //vm:qemu_test_ext4 --test_timeout=900  # 15 minutes
```

## Advanced Usage

### Custom Test Scenarios

```bash
# Many readers, few writers (read-heavy):
bazel run //chaos:simple_chaos_test -- --readers 50 --writers 1 --duration 60 /tmp/test

# Many writers, few readers (write-heavy):
bazel run //chaos:simple_chaos_test -- --readers 2 --writers 10 --duration 60 /tmp/test

# Maximum chaos:
bazel run //chaos:simple_chaos_test -- --readers 100 --writers 20 --duration 300 /tmp/test
```

### Different eBPF Delay Profiles

```bash
# Subtle delays (catch rare bugs):
bazel run //chaos:pause_controller -- 10 200 20000  # 10%, 2μs max

# Moderate delays (default):
bazel run //chaos:pause_controller -- 50 500 50000  # 50%, 5μs typical, 50μs max

# Aggressive delays (stress test):
bazel run //chaos:pause_controller -- 100 1000 200000  # 100%, 10-200μs
```

### Analyzing Specific Time Windows

```bash
# Extract bugs from first 10 seconds:
cat xibalba-bugs.jsonl | jq 'select(.ts < 10000000000)'

# Extract bugs from specific period:
cat xibalba-bugs.jsonl | jq 'select(.ts >= 10000000000 and .ts < 20000000000)'

# Group bugs by type:
cat xibalba-bugs.jsonl | jq -s 'group_by(.missing > 0, .phantoms > 0, .duplicates > 0)'
```

## Performance Benchmarking

### Measure Baseline (No eBPF)

```bash
bazel run //chaos:simple_chaos_test -- --duration 10 --json /tmp/test > baseline.json
```

### Measure With eBPF Delays

Terminal 1:
```bash
bazel run //chaos:pause_controller -- 50 500
```

Terminal 2:
```bash
bazel run //chaos:simple_chaos_test -- --duration 10 --json /tmp/test > with-delays.json
```

### Compare

```bash
jq '.results.ops_per_second' baseline.json
jq '.results.ops_per_second' with-delays.json
jq '.results.bugs_found' with-delays.json
```

**Expected**:
- Baseline: 40,000+ ops/sec, ~0 bugs
- With delays: 10,000-20,000 ops/sec, 100+ bugs

## References

**Academic Papers**:
- Lamport (1978): [PDF](https://lamport.azurewebsites.net/pubs/time-clocks.pdf) | [DOI](https://doi.org/10.1145/359545.359563)
- Fidge (1988): Vector Clocks
- Mattern (1989): Virtual Time
- Herlihy & Wing (1990): Linearizability

**Jepsen**:
- https://jepsen.io/
- https://github.com/jepsen-io/jepsen
- Kyle Kingsbury's talks: https://aphyr.com/tags/jepsen

**eBPF**:
- https://ebpf.io/
- BPF CO-RE: https://nakryiko.com/posts/bpf-core-reference-guide/

---

*Enter Xibalba. Face the trials. Emerge victorious.*
