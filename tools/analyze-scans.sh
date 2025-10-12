#!/bin/bash
# Analyze exported scan data with different consistency model
# Uses xibalba-scans.jsonl to compute bug rates without re-running tests

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 SCANS_FILE MODEL"
    echo ""
    echo "Models:"
    echo "  posix     - Only duplicates are bugs"
    echo "  weak      - Missing + phantoms + duplicates are bugs"
    echo "  strict    - Everything must be current (not yet implemented)"
    echo ""
    echo "Example:"
    echo "  $0 /test/.output/xibalba-scans.jsonl weak"
    echo ""
    exit 1
fi

SCANS_FILE="$1"
MODEL="$2"

if [ ! -f "$SCANS_FILE" ]; then
    echo "ERROR: Scans file not found: $SCANS_FILE"
    exit 1
fi

echo "========================================="
echo " POST-HOC CONSISTENCY ANALYSIS"
echo "========================================="
echo ""
echo "Scans file: $SCANS_FILE"
echo "Model: $MODEL"
echo ""

# Count total scans
TOTAL_SCANS=$(wc -l < "$SCANS_FILE")
echo "Total scans: $TOTAL_SCANS"
echo ""

# Analyze based on model (re-compute from actual vs expected, not pre-computed results!)
case "$MODEL" in
    posix)
        echo "Applying POSIX model (duplicates only)..."
        # Check for duplicates by comparing actual entries
        BUGS=$(jq -r '
            .actual as $arr |
            ($arr | length) as $len |
            ($arr | unique | length) as $unique |
            if $len != $unique then 1 else empty end
        ' "$SCANS_FILE" | wc -l)
        ;;
    weak)
        echo "Applying WEAK model (missing + phantoms + duplicates)..."
        # Check if actual != expected (any difference is a bug)
        BUGS=$(jq -r '
            (.actual | sort) as $actual_sorted |
            (.expected | sort) as $expected_sorted |
            if $actual_sorted != $expected_sorted then 1 else empty end
        ' "$SCANS_FILE" | wc -l)
        ;;
    *)
        echo "ERROR: Unknown model: $MODEL"
        exit 1
        ;;
esac

# Calculate rate
if [ "$TOTAL_SCANS" -gt 0 ]; then
    BUG_RATE=$(awk "BEGIN {printf \"%.2f\", ($BUGS / $TOTAL_SCANS) * 1000}")
    PERCENT=$(awk "BEGIN {printf \"%.1f\", ($BUGS / $TOTAL_SCANS) * 100}")
else
    BUG_RATE="0.00"
    PERCENT="0.0"
fi

echo ""
echo "========================================="
echo " RESULTS (Model: $MODEL)"
echo "========================================="
echo ""
echo "Scans with bugs: $BUGS / $TOTAL_SCANS"
echo "Bug rate: $BUG_RATE per 1000 scans"
echo "Percentage affected: $PERCENT%"
echo ""

if [ "$BUGS" -eq 0 ]; then
    echo "✅ NO BUGS DETECTED with $MODEL model"
else
    echo "🐛 BUGS DETECTED with $MODEL model"
fi

echo ""
echo "Breakdown (re-computed from actual vs expected):"

# Re-compute breakdown based on actual vs expected arrays
TOTAL_MISSING=$(jq -r '
    (.expected | length) - (.actual | length) as $diff |
    if $diff > 0 then $diff else 0 end
' "$SCANS_FILE" | awk '{s+=$1} END {print s}')

TOTAL_PHANTOM=$(jq -r '
    (.actual | length) - (.expected | length) as $diff |
    if $diff > 0 then $diff else 0 end
' "$SCANS_FILE" | awk '{s+=$1} END {print s}')

# Count duplicate instances
TOTAL_DUPLICATES=$(jq -r '
    .actual as $arr |
    ($arr | length) - ($arr | unique | length)
' "$SCANS_FILE" | awk '{s+=$1} END {print s}')

echo "  Duplicates: $TOTAL_DUPLICATES"
echo "  Missing: $TOTAL_MISSING (files expected but not in actual)"
echo "  Phantom: $TOTAL_PHANTOM (files in actual but not expected)"

echo ""
echo "This is POST-HOC ANALYSIS - no test re-run needed!"
echo ""

