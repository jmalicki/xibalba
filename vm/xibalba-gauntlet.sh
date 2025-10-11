#!/bin/bash
set -euo pipefail

# Xibalba Gauntlet - Progressive Consistency Testing
#
# Runs the same test parameters across all filesystems with escalating
# consistency models to quantify departures from ideal behavior.
#
# Testing Strategy:
#   1. EVENTUAL (baseline) - Should always pass (only duplicates are bugs)
#   2. WEAK_POSIX (POSIX standard) - Should pass for POSIX filesystems
#   3. STRICT (linearizable) - Quantify departures (expected to show weak consistency)

DURATION="${XIBALBA_DURATION:-300}"  # 5 minutes per test
READERS="${XIBALBA_READERS:-10}"
WRITERS="${XIBALBA_WRITERS:-3}"
RESULTS_DIR="${XIBALBA_RESULTS:-/var/log/xibalba/gauntlet}"

# Filesystems to test (in order)
FILESYSTEMS=(
    "ext4"
    "xfs"
    "btrfs"
    "zfs"
    "tmpfs"
)

# Consistency models (in order of strictness)
MODELS=(
    "eventual"   # Baseline - only duplicates are bugs
    "weak"       # POSIX weak - snapshot at read start
    "strict"     # Linearizable - all ops instantly visible
)

mkdir -p "$RESULTS_DIR"

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
SUMMARY_FILE="$RESULTS_DIR/gauntlet-summary-${TIMESTAMP}.json"
SUMMARY_TXT="$RESULTS_DIR/gauntlet-summary-${TIMESTAMP}.txt"

echo "════════════════════════════════════════════════════════════════"
echo "    XIBALBA GAUNTLET - The Six Houses of Testing"
echo "════════════════════════════════════════════════════════════════"
echo
echo "Test Parameters (SAME FOR ALL):"
echo "  Duration: $DURATION seconds ($(($DURATION / 60)) minutes)"
echo "  Readers: $READERS threads"
echo "  Writers: $WRITERS threads"
echo "  Filesystems: ${FILESYSTEMS[*]}"
echo "  Models: ${MODELS[*]}"
echo
echo "Testing Strategy:"
echo "  1. EVENTUAL → Should PASS (baseline)"
echo "  2. WEAK     → Should PASS (POSIX guarantee)"
echo "  3. STRICT   → Quantify departures (expected weak consistency)"
echo
echo "Results: $RESULTS_DIR"
echo "════════════════════════════════════════════════════════════════"
echo

# Initialize summary
cat > "$SUMMARY_FILE" <<EOF
{
  "timestamp": "$(date -Iseconds)",
  "duration_seconds": $DURATION,
  "readers": $READERS,
  "writers": $WRITERS,
  "filesystems": $(printf '%s\n' "${FILESYSTEMS[@]}" | jq -R . | jq -s .),
  "models": $(printf '%s\n' "${MODELS[@]}" | jq -R . | jq -s .),
  "results": []
}
EOF

# Setup filesystem helper
setup_filesystem() {
    local fs=$1
    
    case $fs in
        ext4)
            if [ ! -f /data/ext4.img ]; then
                dd if=/dev/zero of=/data/ext4.img bs=1M count=1000 status=none
                mkfs.ext4 -q /data/ext4.img
            fi
            mkdir -p /mnt/test
            umount /mnt/test 2>/dev/null || true
            mount -o loop /data/ext4.img /mnt/test
            echo "/mnt/test"
            ;;
        xfs)
            if [ ! -f /data/xfs.img ]; then
                dd if=/dev/zero of=/data/xfs.img bs=1M count=1000 status=none
                mkfs.xfs -q /data/xfs.img
            fi
            mkdir -p /mnt/test
            umount /mnt/test 2>/dev/null || true
            mount -o loop /data/xfs.img /mnt/test
            echo "/mnt/test"
            ;;
        btrfs)
            if [ ! -f /data/btrfs.img ]; then
                dd if=/dev/zero of=/data/btrfs.img bs=1M count=1000 status=none
                mkfs.btrfs -q /data/btrfs.img
            fi
            mkdir -p /mnt/test
            umount /mnt/test 2>/dev/null || true
            mount -o loop /data/btrfs.img /mnt/test
            echo "/mnt/test"
            ;;
        zfs)
            if ! zpool list testpool >/dev/null 2>&1; then
                dd if=/dev/zero of=/data/zfs.img bs=1M count=2000 status=none
                zpool create testpool /data/zfs.img
                zfs create testpool/testfs
            fi
            echo "/testpool/testfs"
            ;;
        tmpfs)
            mkdir -p /mnt/test
            umount /mnt/test 2>/dev/null || true
            mount -t tmpfs -o size=500M tmpfs /mnt/test
            echo "/mnt/test"
            ;;
        *)
            echo "Unknown filesystem: $fs" >&2
            exit 1
            ;;
    esac
}

# Run test for one filesystem + model combination
run_test() {
    local fs=$1
    local model=$2
    local test_dir=$3
    
    local result_json="$RESULTS_DIR/${fs}-${model}-${TIMESTAMP}.json"
    
    echo "  Testing $fs with $model model..."
    
    # Run test
    local status
    local bugs
    if simple_chaos_test \
        --json \
        --"$model" \
        --duration "$DURATION" \
        --readers "$READERS" \
        --writers "$WRITERS" \
        "$test_dir" > "$result_json" 2>&1; then
        
        status="PASS"
        bugs=0
    else
        status="FAIL"
        bugs=$(jq -r '.results.bugs_found // 0' "$result_json" 2>/dev/null || echo "0")
    fi
    
    # Extract metrics
    local ops
    local missing
    local phantom
    local duplicate
    ops=$(jq -r '.results.total_operations // 0' "$result_json" 2>/dev/null || echo "0")
    missing=$(jq -r '.results.missing_entries // 0' "$result_json" 2>/dev/null || echo "0")
    phantom=$(jq -r '.results.phantom_entries // 0' "$result_json" 2>/dev/null || echo "0")
    duplicate=$(jq -r '.results.duplicate_entries // 0' "$result_json" 2>/dev/null || echo "0")
    
    echo "    Status: $status | Bugs: $bugs | Ops: $ops | Missing: $missing | Phantom: $phantom | Duplicate: $duplicate"
    
    # Append to summary
    local temp_summary
    temp_summary=$(mktemp)
    jq --arg fs "$fs" \
       --arg model "$model" \
       --arg status "$status" \
       --argjson bugs "$bugs" \
       --argjson ops "$ops" \
       --argjson missing "$missing" \
       --argjson phantom "$phantom" \
       --argjson duplicate "$duplicate" \
       --arg result_file "$result_json" \
       '.results += [{
           filesystem: $fs,
           model: $model,
           status: $status,
           bugs_found: $bugs,
           total_operations: $ops,
           missing_entries: $missing,
           phantom_entries: $phantom,
           duplicate_entries: $duplicate,
           result_file: $result_file
       }]' "$SUMMARY_FILE" > "$temp_summary"
    mv "$temp_summary" "$SUMMARY_FILE"
    
    # Cleanup test directory
    rm -rf "${test_dir:?}"/* 2>/dev/null || true
    
    if [ "$status" = "PASS" ]; then
        return 0
    else
        return 1
    fi
}

# Main test loop
total_tests=0
passed_tests=0
failed_tests=0

for fs in "${FILESYSTEMS[@]}"; do
    echo
    echo "┌─────────────────────────────────────────────────────────────┐"
    echo "│ Filesystem: $fs"
    echo "└─────────────────────────────────────────────────────────────┘"
    
    # Setup filesystem
    test_dir=$(setup_filesystem "$fs")
    echo "  Mounted at: $test_dir"
    echo
    
    # Run through all consistency models
    for model in "${MODELS[@]}"; do
        ((total_tests++))
        
        if run_test "$fs" "$model" "$test_dir"; then
            ((passed_tests++))
        else
            ((failed_tests++))
        fi
    done
    
    # Cleanup filesystem
    case $fs in
        ext4|xfs|btrfs|tmpfs)
            umount /mnt/test 2>/dev/null || true
            ;;
        zfs)
            # Leave ZFS mounted for next test
            ;;
    esac
done

# Generate text summary
echo
echo "════════════════════════════════════════════════════════════════"
echo "    GAUNTLET COMPLETE"
echo "════════════════════════════════════════════════════════════════"
echo
echo "Total tests: $total_tests"
echo "Passed: $passed_tests"
echo "Failed: $failed_tests"
echo
echo "Detailed results: $SUMMARY_FILE"
echo

# Create human-readable summary
{
    echo "XIBALBA GAUNTLET SUMMARY"
    echo "Date: $(date)"
    echo "Duration: $DURATION seconds per test"
    echo "Threads: $READERS readers, $WRITERS writers"
    echo
    echo "═══════════════════════════════════════════════════════════════"
    echo
    
    # Per-filesystem summary
    for fs in "${FILESYSTEMS[@]}"; do
        echo "Filesystem: $fs"
        echo "───────────────────────────────────────────────────────────"
        
        for model in "${MODELS[@]}"; do
            result=$(jq -r --arg fs "$fs" --arg model "$model" \
                '.results[] | select(.filesystem == $fs and .model == $model) | 
                "  \(.model | ascii_upcase): \(.status) | Bugs: \(.bugs_found) | Ops: \(.total_operations) | Missing: \(.missing_entries) | Phantom: \(.phantom_entries) | Duplicate: \(.duplicate_entries)"' \
                "$SUMMARY_FILE")
            echo "$result"
        done
        echo
    done
    
    echo "═══════════════════════════════════════════════════════════════"
    echo
    echo "Summary: $total_tests tests, $passed_tests passed, $failed_tests failed"
    
} > "$SUMMARY_TXT"

cat "$SUMMARY_TXT"

# Create symlinks to latest
ln -sf "$SUMMARY_FILE" "$RESULTS_DIR/latest-gauntlet.json"
ln -sf "$SUMMARY_TXT" "$RESULTS_DIR/latest-gauntlet.txt"

# Exit with appropriate code
if [ "$failed_tests" -gt 0 ]; then
    echo "❌ Some tests failed"
    exit 1
else
    echo "✅ All tests passed"
    exit 0
fi

