#!/bin/bash
set -euo pipefail

# Xibalba Automated Test Runner for VMs
#
# Installed as /usr/bin/xibalba-test-runner in VMs
# Automatically sets up filesystem and runs tests

FILESYSTEM="${XIBALBA_FILESYSTEM:-${1:-ext4}}"
TEST_DURATION="${XIBALBA_DURATION:-${2:-300}}"
CONSISTENCY_MODEL="${XIBALBA_MODEL:-${3:-weak}}"
RESULTS_DIR="${XIBALBA_RESULTS:-${4:-/var/log/xibalba}}"

echo "=== Xibalba Automated Test Runner ==="
echo "Filesystem: $FILESYSTEM"
echo "Duration: $TEST_DURATION seconds ($((TEST_DURATION / 60)) minutes)"
echo "Model: $CONSISTENCY_MODEL"
echo "Results: $RESULTS_DIR"
echo

mkdir -p "$RESULTS_DIR"

# Setup filesystem and return mount point
setup_filesystem() {
    local fs=$1
    
    case $fs in
        ext4)
            echo "Setting up ext4..."
            if [ ! -f /data/ext4.img ]; then
                dd if=/dev/zero of=/data/ext4.img bs=1M count=1000 status=none
                mkfs.ext4 -q /data/ext4.img
            fi
            mkdir -p /mnt/test
            mount -o loop /data/ext4.img /mnt/test 2>/dev/null || true
            echo "  ✓ ext4 mounted at /mnt/test"
            echo "/mnt/test"
            ;;
            
        xfs)
            echo "Setting up XFS..."
            if [ ! -f /data/xfs.img ]; then
                dd if=/dev/zero of=/data/xfs.img bs=1M count=1000 status=none
                mkfs.xfs -q /data/xfs.img
            fi
            mkdir -p /mnt/test
            mount -o loop /data/xfs.img /mnt/test 2>/dev/null || true
            echo "  ✓ XFS mounted at /mnt/test"
            echo "/mnt/test"
            ;;
            
        btrfs)
            echo "Setting up btrfs..."
            if [ ! -f /data/btrfs.img ]; then
                dd if=/dev/zero of=/data/btrfs.img bs=1M count=1000 status=none
                mkfs.btrfs -q /data/btrfs.img
            fi
            mkdir -p /mnt/test
            mount -o loop /data/btrfs.img /mnt/test 2>/dev/null || true
            echo "  ✓ btrfs mounted at /mnt/test"
            echo "/mnt/test"
            ;;
            
        zfs)
            echo "Setting up ZFS..."
            if ! zpool list testpool >/dev/null 2>&1; then
                dd if=/dev/zero of=/data/zfs.img bs=1M count=2000 status=none
                zpool create testpool /data/zfs.img
                zfs create testpool/testfs
            fi
            echo "  ✓ ZFS ready at /testpool/testfs"
            echo "/testpool/testfs"
            ;;
            
        tmpfs)
            echo "Setting up tmpfs..."
            mkdir -p /mnt/test
            mount -t tmpfs -o size=500M tmpfs /mnt/test 2>/dev/null || true
            echo "  ✓ tmpfs mounted at /mnt/test"
            echo "/mnt/test"
            ;;
            
        *)
            echo "❌ Error: Unknown filesystem: $fs"
            echo "Supported: ext4, xfs, btrfs, zfs, tmpfs"
            exit 1
            ;;
    esac
}

# Setup and get test directory
TEST_DIR=$(setup_filesystem "$FILESYSTEM")

# Check if simple_chaos_test exists
if ! command -v simple_chaos_test >/dev/null; then
    echo "❌ Error: simple_chaos_test not found in PATH"
    echo "Install Xibalba package: apt install ./xibalba_0.1.0_amd64.deb"
    exit 1
fi

# Run test with JSON output
echo
echo "🌑 Entering Xibalba - The Dark House Trial..."
echo

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
RESULT_JSON="$RESULTS_DIR/xibalba-${FILESYSTEM}-${TIMESTAMP}.json"
RESULT_TXT="$RESULTS_DIR/xibalba-${FILESYSTEM}-${TIMESTAMP}.txt"

# Run with JSON for machine-readable results
if simple_chaos_test \
    --json \
    --"$CONSISTENCY_MODEL" \
    --duration "$TEST_DURATION" \
    "$TEST_DIR" > "$RESULT_JSON" 2>&1; then
    
    echo "✅ TEST PASSED - No bugs detected"
    echo
    
    # Extract key metrics
    SCANS=$(jq -r '.results.directory_scans // 0' "$RESULT_JSON" 2>/dev/null || echo "N/A")
    OPS_SEC=$(jq -r '.results.ops_per_second // 0' "$RESULT_JSON" 2>/dev/null || echo "N/A")
    
    echo "Metrics:"
    echo "  Directory scans: $SCANS"
    echo "  Ops/second: $OPS_SEC"
    echo "  Results: $RESULT_JSON"
    
    # Create symlink to latest
    ln -sf "$RESULT_JSON" "$RESULTS_DIR/latest-${FILESYSTEM}.json"
    
    exit 0
else
    echo "🐛 TEST FAILED - Bugs detected!"
    echo
    
    # Also save human-readable output
    simple_chaos_test \
        --"$CONSISTENCY_MODEL" \
        --duration "$TEST_DURATION" \
        "$TEST_DIR" > "$RESULT_TXT" 2>&1 || true
    
    # Extract bug count
    BUGS=$(jq -r '.results.bugs_found // 0' "$RESULT_JSON" 2>/dev/null || echo "N/A")
    
    echo "Bug count: $BUGS"
    echo "Results: $RESULT_JSON"
    echo "Full output: $RESULT_TXT"
    
    # Create symlink to latest
    ln -sf "$RESULT_JSON" "$RESULTS_DIR/latest-${FILESYSTEM}.json"
    ln -sf "$RESULT_TXT" "$RESULTS_DIR/latest-${FILESYSTEM}.txt"
    
    exit 1
fi

