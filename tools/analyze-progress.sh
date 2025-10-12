#!/bin/bash
# Analyze xibalba-progress.jsonl files
# Shows time-series bug data in human-readable format

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 PROGRESS_JSONL_FILE"
    echo ""
    echo "Analyzes incremental test results from xibalba-progress.jsonl"
    exit 1
fi

JSONL_FILE="$1"

if [ ! -f "$JSONL_FILE" ]; then
    echo "ERROR: File not found: $JSONL_FILE"
    exit 1
fi

echo "=== Xibalba Progress Analysis ==="
echo ""
echo "File: $JSONL_FILE"
echo "Periods: $(wc -l < "$JSONL_FILE")"
echo ""
echo "Time Series:"
echo "─────────────────────────────────────────────────────────────────"
printf "%-8s %-12s %-12s %-12s %-12s %s\n" "Time" "Operations" "Reads" "Bugs" "Bug Rate" "Ops/sec"
echo "─────────────────────────────────────────────────────────────────"

jq -r '"\(.elapsed)s \(.period_ops) \(.period_reads) \(.period_bugs) \(.period_bug_rate * 100)% \(.ops_per_sec)"' "$JSONL_FILE" | \
while read time ops reads bugs rate ops_sec; do
    printf "%-8s %-12s %-12s %-12s %-12s %s\n" "$time" "$ops" "$reads" "$bugs" "$rate" "$ops_sec"
done

echo "─────────────────────────────────────────────────────────────────"
echo ""

# Calculate totals
TOTAL_OPS=$(jq -s 'map(.ops) | max' "$JSONL_FILE")
TOTAL_READS=$(jq -s 'map(.reads) | max' "$JSONL_FILE")
TOTAL_BUGS=$(jq -s 'map(.bugs) | max' "$JSONL_FILE")
FINAL_RATE=$(jq -s 'map(.period_bug_rate) | add / length * 100' "$JSONL_FILE")

echo "Summary:"
echo "  Total operations: $TOTAL_OPS"
echo "  Total reads: $TOTAL_READS"
echo "  Total bugs: $TOTAL_BUGS"
printf "  Average bug rate: %.2f%%\n" "$FINAL_RATE"
echo ""

# Check for timeout
LAST_TIME=$(jq -s 'map(.elapsed) | max' "$JSONL_FILE")
echo "Test duration: ${LAST_TIME}s"
if [ "$TOTAL_BUGS" -gt 0 ]; then
    echo "Status: 🐛 BUGS DETECTED"
else
    echo "Status: ✅ NO BUGS"
fi

