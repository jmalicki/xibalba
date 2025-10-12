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

# Seed entropy (mkfs tools often need random data)
echo "Seeding entropy pool..."
dd if=/dev/zero of=/dev/urandom bs=512 count=1 2>/dev/null || true

# Check if mkfs tool exists
case "$FILESYSTEM" in
    ext4)
        if ! command -v mkfs.ext4 >/dev/null; then
            echo "ERROR: mkfs.ext4 not found"
            exit 1
        fi
        echo "Running mkfs.ext4 with 30s timeout..."
        timeout 30 mkfs.ext4 -F /dev/vda || {
            CODE=$?
            echo "ERROR: mkfs.ext4 failed (exit: $CODE)"
            exit $CODE
        }
        echo "✓ mkfs.ext4 completed"
        ;;
    xfs)
        if ! command -v mkfs.xfs >/dev/null; then
            echo "ERROR: mkfs.xfs not found"
            exit 1
        fi
        echo "Running mkfs.xfs with 30s timeout..."
        timeout 30 mkfs.xfs -f /dev/vda || {
            CODE=$?
            echo "ERROR: mkfs.xfs failed (exit: $CODE)"
            exit $CODE
        }
        echo "✓ mkfs.xfs completed"
        ;;
    btrfs)
        if ! command -v mkfs.btrfs >/dev/null; then
            echo "ERROR: mkfs.btrfs not found"
            exit 1
        fi
        echo "Running mkfs.btrfs with 30s timeout..."
        timeout 30 mkfs.btrfs -f /dev/vda || {
            CODE=$?
            echo "ERROR: mkfs.btrfs failed (exit: $CODE)"
            exit $CODE
        }
        echo "✓ mkfs.btrfs completed"
        ;;
    zfs)
        if ! command -v zpool >/dev/null; then
            echo "ERROR: zpool not found (ZFS not available in initramfs)"
            exit 1
        fi
        
        # Load ZFS kernel modules
        echo "Loading ZFS kernel modules..."
        timeout 10 modprobe zfs 2>/dev/null || {
            CODE=$?
            echo "ERROR: Failed to load ZFS kernel module (exit: $CODE)"
            echo "ZFS may not be available in this kernel"
            exit $CODE
        }
        echo "✓ ZFS module loaded"
        
        # ZFS requires a pool
        echo "Creating ZFS pool with 30s timeout..."
        timeout 30 zpool create -f xibalba-test /dev/vda || {
            CODE=$?
            echo "ERROR: zpool create failed (exit: $CODE)"
            exit $CODE
        }
        echo "✓ ZFS pool created"
        
        echo "Creating ZFS filesystem..."
        timeout 10 zfs create xibalba-test/testdir || {
            CODE=$?
            echo "ERROR: zfs create failed (exit: $CODE)"
            exit $CODE
        }
        
        mkdir -p /test
        timeout 10 zfs set mountpoint=/test xibalba-test/testdir || {
            CODE=$?
            echo "ERROR: zfs set mountpoint failed (exit: $CODE)"
            exit $CODE
        }
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
    if ! mount /dev/vda /test; then
        CODE=$?
        echo "ERROR: Failed to mount $FILESYSTEM (exit: $CODE)"
        echo "This likely means the kernel doesn't support $FILESYSTEM"
        echo "Skipping test execution..."
        echo ""
        echo "=== XIBALBA_TEST_COMPLETE ==="
        echo "EXIT_CODE=255"
        echo "FILESYSTEM=$FILESYSTEM"
        echo "ERROR=mount_failed"
        echo "=========================="
        poweroff
        exit 255
    fi
fi
echo "✓ Test filesystem ready"

# Run xibalba tests directly
# We already have the filesystem mounted at /test, so run simple_chaos_test directly
echo ""
echo "=== Running Xibalba Tests ==="
echo "  Filesystem: $FILESYSTEM"
echo "  Duration: ${DURATION}s"
echo "  Readers: $READERS"
echo "  Writers: $WRITERS"
echo "  Model: ${MODEL:-posix}"
echo "  Test directory: /test"
echo "  Start time: $(date)"
echo ""

cd /test || { echo "ERROR: Failed to cd to /test"; exit 1; }
EXIT_CODE=0
echo "Executing: simple_chaos_test --${MODEL:-posix} --duration $DURATION --readers $READERS --writers $WRITERS /test"
simple_chaos_test --${MODEL:-posix} --duration $DURATION --readers $READERS --writers $WRITERS /test || EXIT_CODE=$?

echo ""
echo "Test completed with exit code: $EXIT_CODE"
echo "End time: $(date)"

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

