#!/bin/bash
# Comprehensive filesystem consistency comparison
# Runs all filesystems with different consistency models in parallel

set -euo pipefail

# Ensure we're in the workspace root
if [ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]; then
    cd "$BUILD_WORKSPACE_DIRECTORY"
fi

# Configuration
DURATION=${DURATION:-30}
READERS=${READERS:-4}
WRITERS=${WRITERS:-2}
RESULTS_DIR=${RESULTS_DIR:-"test-results/comparison-$(date +%Y%m%d-%H%M%S)"}
FILESYSTEMS=("ext4" "xfs" "btrfs")
MODELS=("posix" "weak")

# Create results directory
mkdir -p "$RESULTS_DIR" || {
    echo "ERROR: Failed to create results directory: $RESULTS_DIR"
    echo "PWD: $(pwd)"
    exit 1
}

echo "========================================="
echo " FILESYSTEM CONSISTENCY COMPARISON"
echo "========================================="
echo ""
echo "Configuration:"
echo "  Duration: ${DURATION}s"
echo "  Readers: $READERS"
echo "  Writers: $WRITERS"
echo "  Results: $RESULTS_DIR"
echo ""
echo "Test Matrix:"
echo "  Filesystems: ${FILESYSTEMS[*]}"
echo "  Models: ${MODELS[*]}"
echo "  Total tests: $((${#FILESYSTEMS[@]} * ${#MODELS[@]}))"
echo ""
echo "Running tests in parallel..."
echo ""

# Array to track background jobs
pids=()

# Function to run a single test
run_test() {
    local fs=$1
    local model=$2
    local logfile="$RESULTS_DIR/${fs}_${model}.log"
    
    echo "  Starting: $fs --$model"
    
    # Always use bazel run (works from anywhere)
    bazel run //vm:qemu_test_runner -- \
        --filesystem "$fs" \
        --model "$model" \
        --duration "$DURATION" \
        --readers "$READERS" \
        --writers "$WRITERS" \
        > "$logfile" 2>&1
    
    local exit_code=$?
    echo "$exit_code" > "$RESULTS_DIR/${fs}_${model}.exit"
    
    echo "  Completed: $fs --$model (exit: $exit_code)"
}

# Launch all tests in parallel
for fs in "${FILESYSTEMS[@]}"; do
    for model in "${MODELS[@]}"; do
        run_test "$fs" "$model" &
        pids+=($!)
        # Small delay to avoid overwhelming Bazel
        sleep 2
    done
done

echo ""
echo "Waiting for all tests to complete..."
echo "PIDs: ${pids[*]}"
echo ""

# Wait for all background jobs
for pid in "${pids[@]}"; do
    wait "$pid" || echo "  Warning: PID $pid failed"
done

echo ""
echo "========================================="
echo " TEST RESULTS"
echo "========================================="
echo ""

# Generate summary
summary_file="$RESULTS_DIR/summary.txt"
{
    echo "FILESYSTEM CONSISTENCY COMPARISON RESULTS"
    echo "=========================================="
    echo ""
    echo "Test Configuration:"
    echo "  Date: $(date)"
    echo "  Duration: ${DURATION}s per test"
    echo "  Workload: $READERS readers, $WRITERS writers"
    echo ""
    echo "Results by Filesystem and Model:"
    echo ""
    
    for fs in "${FILESYSTEMS[@]}"; do
        echo "=== $fs ==="
        echo ""
        for model in "${MODELS[@]}"; do
            logfile="$RESULTS_DIR/${fs}_${model}.log"
            exitfile="$RESULTS_DIR/${fs}_${model}.exit"
            
            if [ -f "$exitfile" ]; then
                exit_code=$(cat "$exitfile")
                echo "  --$model: exit=$exit_code"
                
                # Extract bug rate
                if grep -q "Bug rate:" "$logfile" 2>/dev/null; then
                    bug_rate=$(grep "Bug rate:" "$logfile" | head -1 | awk '{print $3, $4}')
                    echo "    Bug rate: $bug_rate"
                fi
                
                # Extract status
                if grep -q "Status:" "$logfile" 2>/dev/null; then
                    status=$(grep "Status:" "$logfile" | head -1 | cut -d: -f2- | xargs)
                    echo "    Status: $status"
                fi
                
                # Check for errors
                if [ "$exit_code" -ne 0 ] && grep -q "ERROR=" "$logfile" 2>/dev/null; then
                    error=$(grep "ERROR=" "$logfile" | head -1 | cut -d= -f2)
                    echo "    Error: $error"
                fi
            else
                echo "  --$model: FAILED (no exit code)"
            fi
            echo ""
        done
        echo ""
    done
    
    echo "=========================================="
    echo ""
    echo "Detailed logs in: $RESULTS_DIR/"
    
} | tee "$summary_file"

echo ""
echo "Summary saved to: $summary_file"
echo ""
echo "View detailed results:"
for fs in "${FILESYSTEMS[@]}"; do
    for model in "${MODELS[@]}"; do
        echo "  cat $RESULTS_DIR/${fs}_${model}.log"
    done
done
echo ""

# Generate markdown report
markdown_file="$RESULTS_DIR/REPORT.md"
{
    echo "# Filesystem Consistency Comparison Report"
    echo ""
    echo "**Date**: $(date)"
    echo "**Duration**: ${DURATION}s per test"
    echo "**Workload**: $READERS readers, $WRITERS writers"
    echo ""
    echo "## Summary Table"
    echo ""
    echo "| Filesystem | POSIX (bugs/1000) | Weak (bugs/1000) |"
    echo "|------------|-------------------|------------------|"
    
    for fs in "${FILESYSTEMS[@]}"; do
        posix_rate="N/A"
        weak_rate="N/A"
        
        # Extract POSIX rate
        if [ -f "$RESULTS_DIR/${fs}_posix.log" ]; then
            posix_rate=$(grep "Bug rate:" "$RESULTS_DIR/${fs}_posix.log" 2>/dev/null | \
                        head -1 | awk '{print $3}' || echo "N/A")
        fi
        
        # Extract weak rate
        if [ -f "$RESULTS_DIR/${fs}_weak.log" ]; then
            weak_rate=$(grep "Bug rate:" "$RESULTS_DIR/${fs}_weak.log" 2>/dev/null | \
                       head -1 | awk '{print $3}' || echo "N/A")
        fi
        
        echo "| **$fs** | $posix_rate | $weak_rate |"
    done
    
    echo ""
    echo "## Interpretation"
    echo ""
    echo "- **POSIX Model**: Tests for duplicate entries (POSIX violation)"
    echo "- **Weak Model**: Tests for stale data, phantom entries, missing files"
    echo ""
    echo "Lower numbers = stronger consistency"
    echo ""
    
} > "$markdown_file"

echo "Markdown report: $markdown_file"
echo ""
echo "========================================="
echo " COMPARISON COMPLETE"
echo "========================================="

