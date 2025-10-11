# Xibalba Benchmarks

**Empirical measurement of bug detection rates**

---

## Purpose

The documentation makes claims about "typical bug rates" for different filesystems. This directory provides tools to **measure those claims empirically** and ensure documentation accuracy.

---

## Quick Start

```bash
# Build test binary
bazel build //chaos:simple_chaos_test

# Run benchmark (measures bug rates on your system)
./benchmarks/measure_bug_rates.sh

# Results saved to benchmarks/results/
```

---

## What Gets Measured

### Bug Detection Rates

For each filesystem × consistency model combination:
- **Total bugs found** over N iterations
- **Bug rate** (bugs per directory scan)
- **Bugs per 1000 scans** (easier to interpret)

### Comparisons

- **Consistency model sensitivity**: STRICT vs WEAK vs EVENTUAL
- **Filesystem differences**: ext4 vs XFS vs btrfs vs tmpfs
- **eBPF impact**: With delays vs without delays

---

## Running Benchmarks

### Basic Usage

```bash
# Measure bug rates on available filesystems
./benchmarks/measure_bug_rates.sh
```

**Prompts for**:
- Iterations per model (default: 10)
- Which filesystems to test

**Output**:
- JSON data files in `benchmarks/results/`
- Summary report in `benchmarks/results/SUMMARY.md`

### Custom Filesystem Testing

```bash
# Set custom test directories
export EXT4_TEST_DIR=/mnt/my_ext4
export XFS_TEST_DIR=/mnt/my_xfs
export BTRFS_TEST_DIR=/mnt/my_btrfs

./benchmarks/measure_bug_rates.sh
```

### Manual Testing

```bash
# Single test with JSON output
bazel-bin/chaos/simple_chaos_test --json --weak /tmp/test > result.json

# Parse results
jq '.results.bugs_found' result.json
jq '.results.bug_rate' result.json
```

---

## Output Format

### JSON Schema

```json
{
  "test": "xibalba_chaos",
  "directory": "/tmp/test",
  "consistency_model": "weak",
  "duration_seconds": 5,
  "reader_threads": 10,
  "writer_threads": 3,
  "results": {
    "total_operations": 90830,
    "directory_scans": 88898,
    "ops_per_second": 18166.0,
    "bugs_found": 0,
    "bug_rate": 0.000000,
    "bugs_per_1000_scans": 0.00
  }
}
```

### Summary Report

Generated in `benchmarks/results/SUMMARY.md`:

```markdown
## tmpfs - weak model
- Total bugs: 0
- Total scans: 88898
- Bug rate: 0.000000
- Bugs per 1000 scans: 0.00

## ext4 - weak model
- Total bugs: 45
- Total scans: 75432
- Bug rate: 0.000597
- Bugs per 1000 scans: 0.60
```

---

## Updating Documentation

### Process

1. **Run benchmarks** on each filesystem
2. **Collect empirical data**
3. **Update** `docs/design/FILESYSTEM-CONSISTENCY-MODELS.md`
4. **Replace** "High/Medium/Low" with actual measured rates

### Example Update

**Before** (assertion):
```markdown
**Bug potential**: High - complex cursor management
```

**After** (measured):
```markdown
**Measured bug rate** (empirical):
- Without eBPF: 0.0006 (0.6 bugs per 1000 scans)
- With eBPF (50% @ 500): 0.0423 (42.3 bugs per 1000 scans)
- Consistency model: WEAK_POSIX
- Measurement: 10 iterations, 2025-01-11
```

---

## Benchmark Scenarios

### Scenario 1: Baseline (No eBPF)

**Purpose**: Measure natural race condition rates

**Command**:
```bash
./benchmarks/measure_bug_rates.sh
```

**Expected**:
- tmpfs: 0-0.1 bugs per 1000 scans
- ext4: 0.2-1.0 bugs per 1000 scans
- XFS: 0.1-0.5 bugs per 1000 scans
- btrfs: 0.05-0.3 bugs per 1000 scans

### Scenario 2: With eBPF Delays

**Purpose**: Measure how eBPF widens race windows

**Commands**:
```bash
# Terminal 1
bazel run //chaos:pause_controller -- 50 500

# Terminal 2
./benchmarks/measure_bug_rates.sh
```

**Expected**:
- tmpfs: 0.5-2.0 bugs per 1000 scans
- ext4: 20-60 bugs per 1000 scans
- XFS: 10-40 bugs per 1000 scans
- btrfs: 5-20 bugs per 1000 scans

### Scenario 3: Consistency Model Comparison

**Purpose**: Validate that STRICT > WEAK > EVENTUAL

**Command**:
```bash
# Run all three models on same filesystem
for model in strict weak eventual; do
  bazel-bin/chaos/simple_chaos_test --json --$model /tmp/test > result_$model.json
done

# Compare bug counts
jq '.results.bugs_found' result_*.json
```

**Expected**: strict_bugs > weak_bugs > eventual_bugs

---

## Analysis Scripts

### Compute Summary Statistics

```bash
# Average bug rate across runs
jq -s 'map(.results.bug_rate) | add/length' benchmarks/results/tmpfs_weak.json

# Standard deviation
jq -s 'map(.results.bug_rate) | (add/length) as $mean | map(pow(. - $mean; 2)) | add/length | sqrt' \
  benchmarks/results/tmpfs_weak.json
```

### Compare Filesystems

```bash
# Extract bug rates for comparison
for fs in tmpfs ext4 xfs btrfs; do
  echo -n "$fs: "
  jq -s 'map(.results.bugs_found) | add' benchmarks/results/${fs}_weak.json
done
```

### Plot Results

```python
import json
import matplotlib.pyplot as plt

# Load results
with open('benchmarks/results/ext4_weak.json') as f:
    data = json.load(f)

bug_rates = [run['results']['bug_rate'] for run in data]

plt.hist(bug_rates, bins=20)
plt.xlabel('Bug Rate')
plt.ylabel('Frequency')
plt.title('ext4 Bug Rate Distribution (WEAK model)')
plt.savefig('ext4_bug_distribution.png')
```

---

## CI Integration

### Automated Benchmarking

Add to CI to track bug rates over time:

```yaml
- name: Run benchmark
  run: |
    bazel build //chaos:simple_chaos_test
    bazel-bin/chaos/simple_chaos_test --json --eventual /tmp/test > ci_result.json

- name: Check bug rate
  run: |
    bugs=$(jq '.results.bugs_found' ci_result.json)
    if [ "$bugs" -gt 0 ]; then
      echo "Unexpected bugs on stable kernel!"
      exit 1
    fi

- name: Upload benchmark data
  uses: actions/upload-artifact@v4
  with:
    name: benchmark-results
    path: ci_result.json
```

### Track Over Time

```bash
# Collect results from multiple CI runs
# Analyze trends over time
# Detect regressions in validation logic
```

---

## Expected Results Template

For documentation in `FILESYSTEM-CONSISTENCY-MODELS.md`:

```markdown
### ext4

**Measured bug detection rates** (empirical data):

| Consistency Model | eBPF Delays | Bug Rate | Bugs per 1K Scans |
|-------------------|-------------|----------|-------------------|
| STRICT | No | 0.0015 | 1.5 |
| WEAK_POSIX | No | 0.0006 | 0.6 |
| EVENTUAL | No | 0.0000 | 0.0 |
| STRICT | Yes (50%@500) | 0.0850 | 85.0 |
| WEAK_POSIX | Yes (50%@500) | 0.0423 | 42.3 |
| EVENTUAL | Yes (50%@500) | 0.0012 | 1.2 |

*Measured: 10 iterations, Linux 6.14.0, ext4 on SSD, 2025-10-11*
```

---

## Why This Matters

**Before**:
```
Bug potential: High - complex cursor management
```
↑ Assertion, no data

**After**:
```
Measured bug rate: 0.0006 (0.6 per 1000 scans, WEAK model, no eBPF)
With eBPF delays: 0.0423 (42.3 per 1000 scans, 70x increase)
Consistency model: WEAK_POSIX
Sample size: 10 iterations × 88K scans = 880K directory operations
Confidence: 95%
```
↑ **Empirical, reproducible, quantitative**

---

## Future Enhancements

1. **Automated regression tracking**: Detect if bug rates change over kernel versions
2. **Statistical analysis**: Confidence intervals, significance testing
3. **Visualization**: Graphs showing bug rate distributions
4. **Comparison reports**: Filesystem A vs B on same hardware

---

**Run `./benchmarks/measure_bug_rates.sh` to validate documentation claims with real data!** 📊

