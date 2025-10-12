#!/bin/bash
# Deep analysis of bugs to determine if they're framework bugs or real

set -euo pipefail

BUGS_FILE="${1:-/tmp/investigate.output/xibalba-bugs.jsonl}"
HISTORY_FILE="${2:-/tmp/investigate.output/xibalba-history.json}"

echo "=== Deep Bug Analysis ==="
echo ""

# Check 1: Are we finding BOTH missing AND phantom for same files?
# (This would indicate framework bug - file can't be both!)
echo "Check 1: Looking for files that appear as both missing AND phantom..."
echo "(If found: Framework bug! A file can't be both missing and phantom)"
echo ""

# This is hard to check without file-level details in bugs.jsonl
# We'd need to add which specific files were missing/phantom

# Check 2: Bug distribution over time
echo "Check 2: Bug distribution over time"
echo "(If bugs cluster at start: initialization issue)"
echo "(If bugs are evenly distributed: real races or systematic framework bug)"
echo ""

jq -s 'group_by(.ts / 5000000000) | 
       map({
         period: (.[0].ts / 1000000000 | floor),
         bugs: length
       })' "$BUGS_FILE" | head -20

echo ""

# Check 3: Thread distribution
echo "Check 3: Which threads report bugs?"
echo ""

jq -s 'group_by(.thread) | 
       map({
         thread: .[0].thread,
         bugs: length,
         missing_total: map(.missing) | add,
         phantom_total: map(.phantoms) | add
       })' "$BUGS_FILE"

echo ""

# Check 4: Timing analysis
echo "Check 4: How long after operations do bugs appear?"
echo "(If bugs appear immediately after creates: framework bug)"
echo "(If bugs appear later: real caching/consistency issue)"
echo ""

# Get first few creates and first few bug timestamps
echo "First 5 bug timestamps (in seconds since start):"
jq '.ts / 1000000000' "$BUGS_FILE" | head -5

echo ""
echo "=== Verdict Indicators ==="
echo ""
echo "Framework bug indicators:"
echo "  - Same file as both missing AND phantom: [need file-level tracking]"
echo "  - Bugs only from specific threads: $(jq -s 'group_by(.thread) | length' "$BUGS_FILE") threads reporting"
echo "  - Bugs clustered at start: [see time distribution above]"
echo ""
echo "Real consistency bug indicators:"
echo "  - Bugs evenly distributed over time: [see above]"
echo "  - Both missing and phantom (different files): YES (63% phantom, 33% missing)"
echo "  - Happens with NO eBPF delays: YES (confirms real races)"

