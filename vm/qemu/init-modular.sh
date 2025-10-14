#!/bin/sh
# Minimal init script for Xibalba Modular Chaos Testing
# This runs as PID 1 in the VM and calls chaos_test_runner

set -e

echo "=== Xibalba Modular Chaos VM Init ==="

# Mount essential filesystems
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev
mkdir -p /run
mount -t tmpfs -o mode=0755 tmpfs /run

# Start udev for dynamic device node creation (needed by ZFS)
echo "Starting udev..."
if command -v udevd >/dev/null 2>&1; then
    mkdir -p /run/udev
    /sbin/udevd --daemon 2>/dev/null && sleep 0.5 || echo "  ⚠️  udevd failed"
    if command -v udevadm >/dev/null 2>&1; then
        udevadm trigger --action=add 2>/dev/null || true
        udevadm settle --timeout=5 2>/dev/null || true
        echo "  ✓ udev running"
    fi
else
    echo "  ⚠️  udev not available"
fi

# Get test parameters from kernel command line
WORKLOAD=$(cat /proc/cmdline | grep -o 'xibalba.workload=[^ ]*' | cut -d= -f2 || echo "create_delete")
INJECTOR=$(cat /proc/cmdline | grep -o 'xibalba.injector=[^ ]*' | cut -d= -f2 || echo "none")
FILESYSTEM=$(cat /proc/cmdline | grep -o 'xibalba.fs=[^ ]*' | cut -d= -f2 || echo "btrfs")
DURATION=$(cat /proc/cmdline | grep -o 'xibalba.duration=[^ ]*' | cut -d= -f2 || echo "60")
PROBABILITY=$(cat /proc/cmdline | grep -o 'xibalba.probability=[^ ]*' | cut -d= -f2 || echo "50")
DELAY=$(cat /proc/cmdline | grep -o 'xibalba.delay=[^ ]*' | cut -d= -f2 || echo "15")
READERS=$(cat /proc/cmdline | grep -o 'xibalba.readers=[^ ]*' | cut -d= -f2 || echo "10")
WRITERS=$(cat /proc/cmdline | grep -o 'xibalba.writers=[^ ]*' | cut -d= -f2 || echo "3")
MODEL=$(cat /proc/cmdline | grep -o 'xibalba.model=[^ ]*' | cut -d= -f2 || echo "posix")

echo "Test configuration:"
echo "  Workload: $WORKLOAD"
echo "  Injector: $INJECTOR"
echo "  Filesystem: $FILESYSTEM"
echo "  Duration: ${DURATION}s"
echo "  Probability: ${PROBABILITY}%"
echo "  Delay: ${DELAY}μs"
echo "  Readers: $READERS"
echo "  Writers: $WRITERS"
echo "  Model: $MODEL"

# Xibalba binaries are embedded in initramfs at /usr/bin
# eBPF programs are at /opt/xibalba/bpf/*.bpf.o
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
export XIBALBA_BPF_PATH=/opt/xibalba/bpf
echo "✓ Xibalba ready (chaos_test_runner + eBPF programs)"

# Format test device (/dev/vda)
echo "Formatting /dev/vda as ${FILESYSTEM}..."

# Seed entropy (mkfs tools often need random data)
echo "Seeding entropy pool..."
dd if=/dev/zero of=/dev/urandom bs=512 count=1 2>/dev/null || true

# Check if mkfs tool exists and format
case "$FILESYSTEM" in
    ext4)
        if ! command -v mkfs.ext4 >/dev/null 2>&1; then
            echo "ERROR: mkfs.ext4 not found in initramfs"
            exit 255
        fi
        mkfs.ext4 -q /dev/vda || { echo "ERROR: mkfs.ext4 failed"; exit 1; }
        ;;
    xfs)
        if ! command -v mkfs.xfs >/dev/null 2>&1; then
            echo "ERROR: mkfs.xfs not found in initramfs"
            exit 255
        fi
        mkfs.xfs -f /dev/vda || { echo "ERROR: mkfs.xfs failed"; exit 1; }
        ;;
    btrfs)
        if ! command -v mkfs.btrfs >/dev/null 2>&1; then
            echo "ERROR: mkfs.btrfs not found in initramfs"
            exit 255
        fi
        mkfs.btrfs -f /dev/vda || { echo "ERROR: mkfs.btrfs failed"; exit 1; }
        ;;
    zfs)
        # ZFS requires kernel modules
        if ! command -v zpool >/dev/null 2>&1; then
            echo "ERROR: zpool not found (ZFS not available in initramfs)"
            exit 255
        fi
        
        # Load ZFS module
        modprobe zfs 2>/dev/null || {
            echo "ERROR: Failed to load ZFS kernel module"
            exit 255
        }
        
        # Create ZFS pool
        zpool create -f xibalba-test /dev/vda || {
            CODE=$?
            echo "ERROR: zpool create failed (exit: $CODE)"
            exit $CODE
        }
        
        # Trigger udev for partition nodes
        if command -v udevadm >/dev/null 2>&1; then
            udevadm trigger --action=add --subsystem-match=block 2>/dev/null || true
            udevadm settle --timeout=5 2>/dev/null || true
        fi
        
        # Create ZFS filesystem
        zfs create xibalba-test/testdir || {
            CODE=$?
            echo "ERROR: zfs create failed (exit: $CODE)"
            exit $CODE
        }
        
        mkdir -p /test
        zfs set mountpoint=/test xibalba-test/testdir || {
            CODE=$?
            echo "ERROR: zfs set mountpoint failed (exit: $CODE)"
            exit $CODE
        }
        echo "✓ ZFS pool created and mounted"
        # Skip standard mount below
        ;;
    *)
        echo "ERROR: Unknown filesystem: $FILESYSTEM"
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
        echo "=== XIBALBA_TEST_COMPLETE ==="
        echo "EXIT_CODE=255"
        echo "ERROR=mount_failed"
        echo "=========================="
        poweroff
        exit 255
    fi
fi
echo "✓ Test filesystem ready at /test"

# Run chaos_test_runner
echo ""
echo "=== Running Xibalba Modular Chaos Test ==="
echo "  Command: chaos_test_runner"
echo "  Workload: $WORKLOAD"
echo "  Injector: $INJECTOR (${PROBABILITY}% @ ${DELAY}μs)"
echo "  Filesystem: $FILESYSTEM"
echo "  Duration: ${DURATION}s"
echo "  Concurrency: ${READERS}r + ${WRITERS}w"
echo "  Model: $MODEL"
echo "  Test dir: /test"
echo "  BPF path: $XIBALBA_BPF_PATH"
echo "  Start: $(date)"
echo ""

cd /test || { echo "ERROR: Failed to cd to /test"; exit 1; }
EXIT_CODE=0

# Build chaos_test_runner command
CMD="chaos_test_runner --workload $WORKLOAD --filesystem $FILESYSTEM --model $MODEL --duration $DURATION --readers $READERS --writers $WRITERS"

# Add injector if not "none"
if [ "$INJECTOR" != "none" ]; then
    CMD="$CMD --injector $INJECTOR --probability $PROBABILITY --delay $DELAY"
fi

# Add test directory
CMD="$CMD /test"

echo "Executing: $CMD"
echo ""

# Run the test
eval $CMD || EXIT_CODE=$?

echo ""
echo "Test completed with exit code: $EXIT_CODE"
echo "End time: $(date)"

# Output test completion marker
echo ""
echo "=== XIBALBA_TEST_COMPLETE ==="
echo "EXIT_CODE=$EXIT_CODE"
echo "WORKLOAD=$WORKLOAD"
echo "INJECTOR=$INJECTOR"
echo "FILESYSTEM=$FILESYSTEM"

# Output progress JSONL
if [ -f /test/xibalba-progress.jsonl ]; then
    echo ""
    echo "=== XIBALBA_PROGRESS_JSONL ==="
    cat /test/xibalba-progress.jsonl
    echo "=== END_XIBALBA_PROGRESS_JSONL ==="
fi

# Output bugs JSONL
if [ -f /test/xibalba-bugs.jsonl ]; then
    echo ""
    echo "=== XIBALBA_BUGS_JSONL ==="
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

# Output eBPF injector stats (if available)
if [ -f /test/xibalba-injector-stats.json ]; then
    echo ""
    echo "=== XIBALBA_INJECTOR_STATS ==="
    cat /test/xibalba-injector-stats.json
    echo "=== END_XIBALBA_INJECTOR_STATS ==="
fi

echo "=========================="

# Shutdown
poweroff
exit $EXIT_CODE

