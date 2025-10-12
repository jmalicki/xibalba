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
MODEL=$(cat /proc/cmdline | grep -o 'xibalba.model=[^ ]*' | cut -d= -f2 || echo "posix")

echo "Test configuration:"
echo "  Filesystem: $FILESYSTEM"
echo "  Consistency model: $MODEL"
echo "  Duration: $DURATION seconds"
echo "  Readers: $READERS"
echo "  Writers: $WRITERS"

# Xibalba binaries are embedded in initramfs at /usr/bin
# No need for 9p mounting!
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
echo "✓ Xibalba binaries ready (embedded in initramfs)"

# Format test device (/dev/vda)
echo "Formatting /dev/vda as ${FILESYSTEM}..."

# Check if mkfs tool exists
case "$FILESYSTEM" in
    ext4)
        if ! command -v mkfs.ext4 >/dev/null; then
            echo "ERROR: mkfs.ext4 not found"
            exit 1
        fi
        mkfs.ext4 -F /dev/vda
        ;;
    xfs)
        if ! command -v mkfs.xfs >/dev/null; then
            echo "ERROR: mkfs.xfs not found"
            exit 1
        fi
        mkfs.xfs -f /dev/vda
        ;;
    btrfs)
        if ! command -v mkfs.btrfs >/dev/null; then
            echo "ERROR: mkfs.btrfs not found"
            exit 1
        fi
        mkfs.btrfs -f /dev/vda
        ;;
    zfs)
        if ! command -v zpool >/dev/null; then
            echo "ERROR: zpool not found (ZFS not available in initramfs)"
            exit 1
        fi
        
        # Load ZFS kernel modules
        echo "Loading ZFS kernel modules..."
        modprobe zfs 2>/dev/null || {
            echo "ERROR: Failed to load ZFS kernel module"
            echo "ZFS may not be available in this kernel"
            exit 1
        }
        
        # ZFS requires a pool
        echo "Creating ZFS pool..."
        zpool create -f xibalba-test /dev/vda
        zfs create xibalba-test/testdir
        mkdir -p /test
        zfs set mountpoint=/test xibalba-test/testdir
        echo "✓ ZFS pool created and mounted"
        # Skip standard mount below
        ;;
    *)
        echo "ERROR: Unknown filesystem: $FILESYSTEM"
        echo "Supported: ext4, xfs, btrfs, zfs"
        exit 1
        ;;
esac

echo "✓ Filesystem formatted"

# Mount test filesystem (unless already mounted by ZFS)
if [ "$FILESYSTEM" != "zfs" ]; then
    echo "Mounting test filesystem..."
    mkdir -p /test
    mount /dev/vda /test
fi
echo "✓ Test filesystem ready"

# Run xibalba tests directly
# We already have the filesystem mounted at /test, so run simple_chaos_test directly
echo ""
echo "=== Running Xibalba Tests ==="
echo "Running simple_chaos_test for $DURATION seconds with $READERS readers and $WRITERS writers..."
echo "Consistency model: ${MODEL:-posix}"
cd /test
EXIT_CODE=0
simple_chaos_test --${MODEL:-posix} --duration $DURATION --readers $READERS --writers $WRITERS /test || EXIT_CODE=$?

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

# Output bugs JSONL (one line per bug with details)
if [ -f /test/xibalba-bugs.jsonl ]; then
    echo ""
    echo "=== XIBALBA_BUGS_JSONL ==="
    # Show first 20 and last 20 bugs if file is large
    TOTAL_BUGS=$(wc -l < /test/xibalba-bugs.jsonl)
    echo "# Total bug events: $TOTAL_BUGS"
    if [ "$TOTAL_BUGS" -gt 40 ]; then
        head -20 /test/xibalba-bugs.jsonl
        echo "# ... ($((TOTAL_BUGS - 40)) bug events omitted) ..."
        tail -20 /test/xibalba-bugs.jsonl
    else
        cat /test/xibalba-bugs.jsonl
    fi
    echo "=== END_XIBALBA_BUGS_JSONL ==="
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

