#!/bin/bash
# Xibalba VM Infrastructure Smoke Test
# Quick validation that VMs boot, package installs, and tests run
# Duration: ~2 minutes (vs 15 minutes for full gauntlet)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Quick test configuration
TEST_DURATION="${TEST_DURATION:-30}"       # 30 seconds (vs 300 for full test)
TEST_READERS="${TEST_READERS:-3}"          # 3 reader threads (vs 10)
TEST_WRITERS="${TEST_WRITERS:-1}"          # 1 writer thread (vs 3)
FILESYSTEMS="${FILESYSTEMS:-tmpfs}"        # Just tmpfs (fastest) for smoke test
CONSISTENCY_MODEL="${CONSISTENCY_MODEL:-WEAK}"  # Single model only

echo "════════════════════════════════════════════════════════════════"
echo "  Xibalba Infrastructure Smoke Test"
echo "════════════════════════════════════════════════════════════════"
echo
echo "Quick validation of:"
echo "  ✓ VM boots successfully"
echo "  ✓ Package installation works"
echo "  ✓ Filesystem setup works"
echo "  ✓ Test execution works"
echo "  ✓ Results collection works"
echo
echo "Configuration:"
echo "  Filesystem: $FILESYSTEMS"
echo "  Model: $CONSISTENCY_MODEL"
echo "  Duration: ${TEST_DURATION}s (quick!)"
echo "  Concurrency: ${TEST_READERS}r + ${TEST_WRITERS}w"
echo
echo "Expected time: ~2 minutes"
echo "════════════════════════════════════════════════════════════════"
echo

# Setup test directory
TEST_DIR="/mnt/test"
mkdir -p "$TEST_DIR"

case $FILESYSTEMS in
    tmpfs)
        echo "Setting up tmpfs..."
        mount -t tmpfs -o size=500M tmpfs "$TEST_DIR"
        ;;
    ext4)
        echo "Setting up ext4..."
        dd if=/dev/zero of=/tmp/ext4.img bs=1M count=500
        mkfs.ext4 -q /tmp/ext4.img
        mount -o loop /tmp/ext4.img "$TEST_DIR"
        ;;
    *)
        echo "ERROR: Unsupported filesystem for smoke test: $FILESYSTEMS"
        echo "Supported: tmpfs, ext4"
        exit 1
        ;;
esac

echo "✓ Filesystem mounted at $TEST_DIR"
echo

# Run single quick test
echo "Running smoke test (${TEST_DURATION}s)..."
START_TIME=$(date +%s)

simple_chaos_test \
    --consistency "$CONSISTENCY_MODEL" \
    --duration "$TEST_DURATION" \
    --readers "$TEST_READERS" \
    --writers "$TEST_WRITERS" \
    "$TEST_DIR"

END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

echo
echo "════════════════════════════════════════════════════════════════"
echo "  Smoke Test Complete"
echo "════════════════════════════════════════════════════════════════"
echo
echo "✅ Infrastructure validated successfully!"
echo "   Duration: ${DURATION}s"
echo "   Filesystem: $FILESYSTEMS"
echo "   Model: $CONSISTENCY_MODEL"
echo

# Cleanup
case $FILESYSTEMS in
    tmpfs)
        umount "$TEST_DIR" 2>/dev/null || true
        ;;
    ext4)
        umount "$TEST_DIR" 2>/dev/null || true
        rm -f /tmp/ext4.img
        ;;
esac

exit 0

