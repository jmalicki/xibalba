#!/bin/bash
set -euo pipefail

# Measure Empirical Bug Detection Rates
# 
# Runs Xibalba tests on different filesystems with different consistency
# models to measure actual bug detection rates and validate documentation claims.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Configuration
ITERATIONS=10
TEST_BINARY="$PROJECT_ROOT/bazel-bin/chaos/simple_chaos_test"
RESULTS_DIR="$PROJECT_ROOT/benchmarks/results"

# Ensure test binary exists
if [ ! -x "$TEST_BINARY" ]; then
    echo "Error: Test binary not found at $TEST_BINARY"
    echo "Run: bazel build //chaos:simple_chaos_test"
    exit 1
fi

mkdir -p "$RESULTS_DIR"

echo "=== Xibalba Bug Rate Measurement ==="
echo ""
echo "This script measures EMPIRICAL bug detection rates across:"
echo "  - 3 consistency models (strict, weak, eventual)"
echo "  - Multiple filesystems (ext4, XFS, btrfs, tmpfs, etc.)"
echo "  - With and without eBPF delays"
echo ""
echo "Results will be saved to: $RESULTS_DIR/"
echo ""
echo "Iterations per configuration: $ITERATIONS"
echo "Total tests: ~$(echo "$ITERATIONS * 3 * 4" | bc) (may take 20-30 minutes)"
echo ""
read -p "Continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    exit 0
fi

# Function to run benchmark on a filesystem
run_benchmark() {
    local fs_name=$1
    local test_dir=$2
    local with_ebpf=$3
    
    local ebpf_suffix=""
    if [ "$with_ebpf" = "yes" ]; then
        ebpf_suffix="_with_ebpf"
    fi
    
    echo "Testing $fs_name (eBPF: $with_ebpf)..."
    
    # Create test directory
    mkdir -p "$test_dir"
    
    # Test each consistency model
    for model in strict weak eventual; do
        local output_file="$RESULTS_DIR/${fs_name}_${model}${ebpf_suffix}.json"
        
        echo "  $model model..."
        
        # Run multiple iterations and collect results
        echo "[" > "$output_file"
        
        for ((i=1; i<=ITERATIONS; i++)); do
            echo -n "    Iteration $i/$ITERATIONS... "
            
            # Run test with JSON output
            if $TEST_BINARY --json --$model "$test_dir" > /tmp/xibalba_result.json 2>/dev/null; then
                echo "0 bugs"
            else
                echo "bugs found"
            fi
            
            # Append to results file
            cat /tmp/xibalba_result.json >> "$output_file"
            if [ $i -lt $ITERATIONS ]; then
                echo "," >> "$output_file"
            fi
        done
        
        echo "]" >> "$output_file"
        
        # Compute summary statistics
        local total_bugs
        local total_scans
        local bug_rate
        total_bugs=$(grep -o '"bugs_found": [0-9]*' "$output_file" | awk '{sum+=$2} END {print sum}')
        total_scans=$(grep -o '"directory_scans": [0-9]*' "$output_file" | awk '{sum+=$2} END {print sum}')
        bug_rate=$(echo "scale=6; $total_bugs / $total_scans" | bc)
        
        echo "    Summary: $total_bugs bugs in $total_scans scans (rate: $bug_rate)"
    done
    
    echo ""
}

# Test tmpfs (baseline - should have lowest bug rate)
echo "=== Testing tmpfs (baseline) ===" 
run_benchmark "tmpfs" "/tmp/xibalba_tmpfs_test" "no"

# Test ext4 (if available)
if [ -d "/mnt/ext4" ] || [ -d "$HOME/ext4_test" ]; then
    echo "=== Testing ext4 ==="
    EXT4_DIR="${EXT4_TEST_DIR:-$HOME/ext4_test}"
    mkdir -p "$EXT4_DIR"
    run_benchmark "ext4" "$EXT4_DIR" "no"
else
    echo "Skipping ext4 (mount point not found)"
    echo "Set EXT4_TEST_DIR=/path/to/ext4/mount to test ext4"
fi

# Test XFS (if available)
if [ -d "/mnt/xfs" ] || [ -d "$HOME/xfs_test" ]; then
    echo "=== Testing XFS ==="
    XFS_DIR="${XFS_TEST_DIR:-$HOME/xfs_test}"
    mkdir -p "$XFS_DIR"
    run_benchmark "xfs" "$XFS_DIR" "no"
else
    echo "Skipping XFS (mount point not found)"
fi

# Generate summary report
echo ""
echo "=== Generating Summary Report ==="

cat > "$RESULTS_DIR/SUMMARY.md" << 'EOF'
# Xibalba Bug Detection Rate Measurements

**Empirical data from actual test runs**

## Methodology

- Iterations per model: $ITERATIONS
- Test duration: 5 seconds per run
- Reader threads: 10
- Writer threads: 3
- eBPF delays: None (baseline measurements)

## Results by Filesystem

EOF

# Process results and add to summary
for fs in tmpfs ext4 xfs btrfs; do
    for model in strict weak eventual; do
        result_file="$RESULTS_DIR/${fs}_${model}.json"
        if [ -f "$result_file" ]; then
            total_bugs=$(grep -o '"bugs_found": [0-9]*' "$result_file" | awk '{sum+=$2} END {print sum}')
            total_scans=$(grep -o '"directory_scans": [0-9]*' "$result_file" | awk '{sum+=$2} END {print sum}')
            
            if [ -n "$total_bugs" ] && [ -n "$total_scans" ]; then
                bug_rate=$(echo "scale=6; $total_bugs / $total_scans" | bc)
                bugs_per_1k=$(echo "scale=2; $bug_rate * 1000" | bc)
                
                echo "### $fs - $model model" >> "$RESULTS_DIR/SUMMARY.md"
                echo "- Total bugs: $total_bugs" >> "$RESULTS_DIR/SUMMARY.md"
                echo "- Total scans: $total_scans" >> "$RESULTS_DIR/SUMMARY.md"
                echo "- Bug rate: $bug_rate" >> "$RESULTS_DIR/SUMMARY.md"
                echo "- Bugs per 1000 scans: $bugs_per_1k" >> "$RESULTS_DIR/SUMMARY.md"
                echo "" >> "$RESULTS_DIR/SUMMARY.md"
            fi
        fi
    done
done

cat >> "$RESULTS_DIR/SUMMARY.md" << 'EOF'

## Key Findings

(To be filled in after analyzing results)

## Update Documentation

Use these empirical measurements to update:
- docs/design/FILESYSTEM-CONSISTENCY-MODELS.md
- Replace "High/Medium/Low" with actual measured rates
- Add confidence intervals
EOF

echo "✅ Benchmark complete!"
echo ""
echo "Results saved to: $RESULTS_DIR/"
echo "Summary report: $RESULTS_DIR/SUMMARY.md"
echo ""
echo "Next steps:"
echo "  1. Review results in $RESULTS_DIR/"
echo "  2. Update documentation with empirical data"
echo "  3. Run with eBPF delays for comparison"
echo ""

