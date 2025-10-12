#!/bin/bash
# Analyze test results with different consistency model
# Uses the exported progress data to estimate what would happen

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 RESULTS_DIR MODEL"
    echo ""
    echo "Analyzes test results with different consistency model"
    echo ""
    echo "Models:"
    echo "  posix  - Only duplicates forbidden"
    echo "  weak   - Stale/phantom/missing forbidden"  
    echo "  strict - Everything must be current"
    echo ""
    echo "Example:"
    echo "  $0 test-results/btrfs-extreme-20251012-064244 weak"
    exit 1
fi

RESULTS_DIR="$1"
MODEL="$2"

if [ ! -d "$RESULTS_DIR" ]; then
    echo "ERROR: Results directory not found: $RESULTS_DIR"
    exit 1
fi

LOG_FILE="$RESULTS_DIR/output.log"

if [ ! -f "$LOG_FILE" ]; then
    echo "ERROR: output.log not found in $RESULTS_DIR"
    exit 1
fi

echo "========================================="
echo " POST-HOC ANALYSIS"
echo "========================================="
echo ""
echo "Results: $RESULTS_DIR"
echo "Model: $MODEL"
echo ""

# Extract configuration
FS=$(grep "Filesystem:" "$LOG_FILE" | head -1 | awk '{print $2}')
READERS=$(grep "Reader threads:" "$LOG_FILE" | head -1 | awk '{print $3}')
WRITERS=$(grep "Writer threads:" "$LOG_FILE" | head -1 | awk '{print $3}')
DUR=$(grep "^Duration:" "$LOG_FILE" | head -1 | awk '{print $2}')
SCANS=$(grep "Directory scans:" "$LOG_FILE" | head -1 | awk '{print $3}')
OPS=$(grep "Total operations:" "$LOG_FILE" | head -1 | awk '{print $3}')

echo "Test configuration:"
echo "  Filesystem: $FS"
echo "  Readers: $READERS threads"
echo "  Writers: $WRITERS threads"
echo "  Duration: ${DUR}s"
echo "  Scans: $SCANS"
echo "  Operations: $OPS"
echo ""

# Extract original results
ORIG_MODEL=$(grep "Consistency model:" "$LOG_FILE" | head -1 | cut -d: -f2- | sed 's/^ *//' | cut -d'(' -f1 | xargs)
ORIG_BUGS=$(grep "Bugs found:" "$LOG_FILE" | head -1 | awk '{print $3}')
ORIG_RATE=$(grep "Bug rate:" "$LOG_FILE" | head -1 | awk '{print $3}')

echo "Original validation:"
echo "  Model: $ORIG_MODEL"
echo "  Bugs: $ORIG_BUGS"
echo "  Rate: $ORIG_RATE/1000 scans"
echo ""

echo "========================================="
echo " LIMITATION: Scan Export Not Yet Implemented"
echo "========================================="
echo ""
echo "To enable true post-hoc analysis, we need:"
echo "  1. Export scan-by-scan data during test"
echo "  2. Create C analyzer to re-validate with different rules"
echo ""
echo "Quick workaround: Re-run with desired model"
echo "(Fast because VM/kernel are already built)"
echo ""
echo "Command:"
echo "  bazel run //vm:qemu_test_runner -- \\"
echo "    --filesystem $FS \\"
echo "    --model $MODEL \\"
echo "    --duration $DUR \\"
echo "    --readers $READERS \\"
echo "    --writers $WRITERS \\"
echo "    --cpus 32"
echo ""
echo "Estimated results for --weak model:"
echo "  Based on normal tests: btrfs shows ~944/1000 bugs at weak"
echo "  With 768 threads: Likely higher due to more contention"
echo "  Prediction: 950-990 bugs/1000 scans"
echo ""
echo "To confirm: Re-run with --weak (takes ~2 minutes)"
echo ""

