#!/bin/sh
# Minimal init script for Xibalba VM testing
# This runs as PID 1 in the VM

set -e

echo "=== Xibalba Fast VM Init ==="

# Mount essential filesystems
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

# Get test parameters from kernel command line
FILESYSTEM=$(cat /proc/cmdline | grep -o 'xibalba.fs=[^ ]*' | cut -d= -f2)
DURATION=$(cat /proc/cmdline | grep -o 'xibalba.duration=[^ ]*' | cut -d= -f2 || echo "300")
READERS=$(cat /proc/cmdline | grep -o 'xibalba.readers=[^ ]*' | cut -d= -f2 || echo "10")
WRITERS=$(cat /proc/cmdline | grep -o 'xibalba.writers=[^ ]*' | cut -d= -f2 || echo "3")

echo "Test configuration:"
echo "  Filesystem: $FILESYSTEM"
echo "  Duration: $DURATION seconds"
echo "  Readers: $READERS"
echo "  Writers: $WRITERS"

# Xibalba binaries are embedded in initramfs at /usr/bin
# No need for 9p mounting!
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
echo "✓ Xibalba binaries ready (embedded in initramfs)"

# Format test device (/dev/vda)
echo "Formatting /dev/vda as ${FILESYSTEM}..."
case "$FILESYSTEM" in
    ext4)
        mkfs.ext4 -F /dev/vda
        ;;
    xfs)
        mkfs.xfs -f /dev/vda
        ;;
    btrfs)
        mkfs.btrfs -f /dev/vda
        ;;
    *)
        echo "ERROR: Unknown filesystem: $FILESYSTEM"
        echo "Supported: ext4, xfs, btrfs"
        exit 1
        ;;
esac

# Mount test filesystem
echo "Mounting test filesystem..."
mkdir -p /test
mount /dev/vda /test
echo "✓ Test filesystem ready"

# Run xibalba tests directly
# We already have the filesystem mounted at /test, so run simple_chaos_test directly
echo ""
echo "=== Running Xibalba Tests ==="
echo "Running simple_chaos_test for $DURATION seconds with $READERS readers and $WRITERS writers..."
cd /test
EXIT_CODE=0
simple_chaos_test --duration $DURATION --readers $READERS --writers $WRITERS /test || EXIT_CODE=$?

echo ""
echo "Test completed with exit code: $EXIT_CODE"

# Output test completion marker
echo ""
echo "=== XIBALBA_TEST_COMPLETE ==="
echo "EXIT_CODE=$EXIT_CODE"
echo "FILESYSTEM=$FILESYSTEM"

# Output progress JSONL (one line per 5-second period)
if [ -f /test/xibalba-progress.jsonl ]; then
    echo ""
    echo "=== XIBALBA_PROGRESS_JSONL ==="
    cat /test/xibalba-progress.jsonl
    echo "=== END_XIBALBA_PROGRESS_JSONL ==="
fi

# Output full history JSON if needed (can be very large)
if [ -f /test/xibalba-history.json ]; then
    echo ""
    echo "=== XIBALBA_HISTORY_JSON ==="
    # Only output first/last parts to avoid huge logs
    head -50 /test/xibalba-history.json
    echo "  ... (truncated, full history in VM) ..."
    tail -20 /test/xibalba-history.json
    echo "=== END_XIBALBA_HISTORY_JSON ==="
fi

echo "=========================="

# Sync and poweroff
sync
echo "Powering off..."
poweroff -f

