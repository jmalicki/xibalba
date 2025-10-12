#!/bin/bash
# Extreme stress test for btrfs POSIX compliance
# 32 CPUs, massive oversubscription, extreme eBPF delays
# Goal: Find POSIX violations (duplicates) under maximum stress

set -euo pipefail

# Ensure we're in the workspace root
if [ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]; then
    cd "$BUILD_WORKSPACE_DIRECTORY"
fi

# Configuration
FILESYSTEM="btrfs"
MODEL="posix"
DURATION=${DURATION:-60}
CPUS=${CPUS:-32}
READERS=${READERS:-512}  # 16x CPUs (MASSIVE oversubscription!)
WRITERS=${WRITERS:-256}  # 8x CPUs (MASSIVE oversubscription!)
RESULTS_DIR=${RESULTS_DIR:-"test-results/btrfs-extreme-$(date +%Y%m%d-%H%M%S)"}

mkdir -p "$RESULTS_DIR"

echo "========================================="
echo " BTRFS EXTREME STRESS TEST"
echo "========================================="
echo ""
echo "Objective: Find POSIX violations under extreme stress"
echo ""
echo "Configuration:"
echo "  Filesystem: $FILESYSTEM"
echo "  Model: $MODEL (duplicates = POSIX violation!)"
echo "  Duration: ${DURATION}s"
echo "  CPUs: $CPUS"
echo "  Readers: $READERS threads ($(awk "BEGIN {print $READERS/$CPUS}")x oversubscribed)"
echo "  Writers: $WRITERS threads ($(awk "BEGIN {print $WRITERS/$CPUS}")x oversubscribed)"
echo "  Results: $RESULTS_DIR"
echo ""
echo "Stress Factors:"
echo "  🔥 2:1 reader oversubscription (context switching)"
echo "  🔥 1:1 writer oversubscription (contention)"
echo "  🔥 32 CPUs (massive parallelism)"
echo "  🔥 POSIX model (duplicates forbidden!)"
echo ""
echo "Expected outcome:"
echo "  If btrfs has ANY bugs at POSIX level, this WILL find them."
echo "  If it passes: btrfs is rock-solid under extreme stress."
echo ""
echo "========================================="
echo ""

# Run the test
echo "Launching extreme stress test..."
echo "Started at: $(date)"
echo ""

bazel run //vm:qemu_test_runner -- \
    --filesystem "$FILESYSTEM" \
    --model "$MODEL" \
    --duration "$DURATION" \
    --readers "$READERS" \
    --writers "$WRITERS" \
    --cpus "$CPUS" \
    2>&1 | tee "$RESULTS_DIR/output.log"

EXIT_CODE=${PIPESTATUS[0]}

echo ""
echo "Completed at: $(date)"
echo ""

# Extract results
echo "========================================="
echo " EXTREME STRESS TEST RESULTS"
echo "========================================="
echo ""

if grep -q "Bug rate:" "$RESULTS_DIR/output.log" 2>/dev/null; then
    BUG_RATE=$(grep "Bug rate:" "$RESULTS_DIR/output.log" | head -1 | awk '{print $3}')
    BUG_COUNT=$(grep "Bugs found:" "$RESULTS_DIR/output.log" | head -1 | awk '{print $3}')
    SCANS=$(grep "Directory scans:" "$RESULTS_DIR/output.log" | head -1 | awk '{print $3}')
    OPS=$(grep "Total operations:" "$RESULTS_DIR/output.log" | head -1 | awk '{print $3}')
    
    echo "Performance:"
    echo "  Total operations: $OPS"
    echo "  Directory scans: $SCANS"
    echo ""
    echo "POSIX Compliance:"
    echo "  Bugs found (duplicates): $BUG_COUNT"
    echo "  Bug rate: $BUG_RATE bugs/1000 scans"
    echo ""
    
    if [ "$BUG_RATE" = "0.000000" ]; then
        echo "🎉 RESULT: btrfs PASSES extreme stress test!"
        echo ""
        echo "✅ NO DUPLICATE ENTRIES under:"
        echo "   - 32 CPUs"
        echo "   - 64 reader threads (2x oversubscription)"
        echo "   - 32 writer threads (1x oversubscription)"
        echo "   - $SCANS directory scans"
        echo ""
        echo "Conclusion: btrfs is POSIX-compliant even under extreme stress."
    else
        echo "🐛 RESULT: btrfs has POSIX VIOLATIONS!"
        echo ""
        echo "❌ Found $BUG_COUNT duplicate entries in $SCANS scans"
        echo "❌ Bug rate: $BUG_RATE per 1000 scans"
        echo ""
        echo "This is a REAL BUG - duplicates violate POSIX.1-2024!"
    fi
else
    echo "⚠️  Could not extract results from log"
    echo "Exit code: $EXIT_CODE"
fi

echo ""
echo "========================================="
echo ""
echo "Full results saved to: $RESULTS_DIR/"
echo "  Output: $RESULTS_DIR/output.log"
echo ""
echo "To analyze bugs (if any):"
echo "  grep 'Bug:' $RESULTS_DIR/output.log"
echo ""

exit $EXIT_CODE

