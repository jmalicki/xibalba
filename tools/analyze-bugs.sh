#!/bin/bash
# Analyze xibalba bug patterns to determine if they're real or framework bugs

set -euo pipefail

BUGS_FILE="${1:-/tmp/investigate.output/xibalba-bugs.jsonl}"

echo "=== Bug Pattern Analysis ==="
echo ""

# Count total bugs
TOTAL=$(wc -l < "$BUGS_FILE")
echo "Total bugs: $TOTAL"
echo ""

# Group by pattern
echo "Bug patterns:"
jq -s 'group_by({missing, phantoms, duplicates}) | 
       map({
         missing: .[0].missing, 
         phantoms: .[0].phantoms, 
         duplicates: .[0].duplicates,
         count: length,
         pct: (length / '"$TOTAL"' * 100)
       }) | 
       sort_by(-.count)' "$BUGS_FILE"

echo ""
echo "=== Sample Bugs ===="
echo ""

# Show first bug of each type
echo "Sample: 1 phantom"
jq 'select(.phantoms == 1 and .missing == 0)' "$BUGS_FILE" | head -1 | jq '.'

echo ""
echo "Sample: 1 missing"
jq 'select(.missing == 1 and .phantoms == 0)' "$BUGS_FILE" | head -1 | jq '.'

echo ""
echo "Sample: 2 phantoms"
jq 'select(.phantoms == 2 and .missing == 0)' "$BUGS_FILE" | head -1 | jq '.'

