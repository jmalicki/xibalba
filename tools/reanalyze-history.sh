#!/bin/bash
# Re-analyze xibalba-history.json with different consistency models
# Allows post-hoc analysis without re-running tests

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 HISTORY_JSON MODEL"
    echo ""
    echo "Models:"
    echo "  posix     - Only duplicates are bugs"
    echo "  weak      - Stale data, phantoms, missing files are bugs"
    echo "  strict    - Everything must be immediately consistent"
    echo "  eventual  - Very lenient, only critical bugs"
    echo ""
    echo "Example:"
    echo "  $0 test-results/btrfs-extreme-*/output.log weak"
    echo ""
    echo "This extracts history from the log and analyzes with specified model."
    exit 1
fi

LOG_FILE="$1"
MODEL="$2"

if [ ! -f "$LOG_FILE" ]; then
    echo "ERROR: Log file not found: $LOG_FILE"
    exit 1
fi

echo "========================================="
echo " POST-HOC ANALYSIS"
echo "========================================="
echo ""
echo "Log file: $LOG_FILE"
echo "Model: $MODEL"
echo ""

# Extract test configuration from log
FILESYSTEM=$(grep "Filesystem:" "$LOG_FILE" | head -1 | awk '{print $2}')
READERS=$(grep "Reader threads:" "$LOG_FILE" | head -1 | awk '{print $3}')
WRITERS=$(grep "Writer threads:" "$LOG_FILE" | head -1 | awk '{print $3}')
DURATION=$(grep "Duration:" "$LOG_FILE" | head -1 | awk '{print $2}')

echo "Original test configuration:"
echo "  Filesystem: $FILESYSTEM"
echo "  Readers: $READERS"
echo "  Writers: $WRITERS"  
echo "  Duration: ${DURATION}s"
echo ""

# Extract original validation results
ORIG_MODEL=$(grep "Consistency model:" "$LOG_FILE" | head -1 | cut -d: -f2 | xargs)
ORIG_BUGS=$(grep "Bugs found:" "$LOG_FILE" | head -1 | awk '{print $3}')
ORIG_RATE=$(grep "Bug rate:" "$LOG_FILE" | head -1 | awk '{print $3}')

echo "Original validation (model: $ORIG_MODEL):"
echo "  Bugs: $ORIG_BUGS"
echo "  Rate: $ORIG_RATE/1000 scans"
echo ""

# TODO: Actually re-analyze the history data
# For now, explain what would happen
echo "========================================="
echo " ANALYSIS WITH MODEL: $MODEL"
echo "========================================="
echo ""
echo "⚠️  Full post-hoc analysis not yet implemented."
echo ""
echo "To implement:"
echo "  1. Export scan data to xibalba-scans.jsonl during test"
echo "  2. Create C tool to parse JSONL and re-validate"
echo "  3. Apply different consistency model rules"
echo ""
echo "Current workaround:"
echo "  Re-run test with desired model (fast with cached build)"
echo ""
echo "Example:"
echo "  bazel run //vm:qemu_test_runner -- \\"
echo "    --filesystem $FILESYSTEM \\"
echo "    --model $MODEL \\"
echo "    --duration $DURATION \\"
echo "    --readers $READERS \\"
echo "    --writers $WRITERS \\"
echo "    --cpus 32"
echo ""
echo "See: docs/design/POST-HOC-ANALYSIS.md for full design"
echo ""

