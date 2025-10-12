#!/bin/bash
# Launch QEMU VM for Xibalba testing
# Uses direct kernel boot for maximum speed

set -euo pipefail

# Default values
FILESYSTEM="ext4"
DURATION=300
READERS=10
WRITERS=3
MODEL="posix"

# Parse named arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --filesystem)
            FILESYSTEM="$2"
            shift 2
            ;;
        --duration)
            DURATION="$2"
            shift 2
            ;;
        --readers)
            READERS="$2"
            shift 2
            ;;
        --writers)
            WRITERS="$2"
            shift 2
            ;;
        --model)
            MODEL="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --filesystem FS   Filesystem to test (ext4, xfs, btrfs, zfs) [default: ext4]"
            echo "  --duration SEC    Test duration in seconds [default: 300]"
            echo "  --readers N       Number of reader threads [default: 10]"
            echo "  --writers N       Number of writer threads [default: 3]"
            echo "  --model MODEL     Consistency model (posix, weak, strict, eventual) [default: posix]"
            echo ""
            echo "Examples:"
            echo "  $0 --filesystem ext4 --model posix --duration 30 --readers 5 --writers 2"
            echo "  $0 --filesystem zfs --model weak --duration 60"
            exit 0
            ;;
        *)
            echo "ERROR: Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Find kernel and initramfs from Bazel runfiles
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check runfiles directory (when running via bazel run)
if [ -d "$SCRIPT_DIR/qemu_test_runner.runfiles/_main/vm" ]; then
    RUNFILES="$SCRIPT_DIR/qemu_test_runner.runfiles/_main/vm"
    KERNEL="$RUNFILES/vmlinuz"
    INITRAMFS="$RUNFILES/initramfs.img"
elif [ -f "$SCRIPT_DIR/vmlinuz" ]; then
    # Files in same directory
    KERNEL="$SCRIPT_DIR/vmlinuz"
    INITRAMFS="$SCRIPT_DIR/initramfs.img"
else
    # Manual run from workspace
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
    KERNEL="$PROJECT_ROOT/bazel-bin/vm/vmlinuz"
    INITRAMFS="$PROJECT_ROOT/bazel-bin/vm/initramfs.img"
fi

# Resolve symlinks (follow -L to get actual files)
if [ -L "$KERNEL" ]; then
    KERNEL=$(readlink -f "$KERNEL")
fi
if [ -L "$INITRAMFS" ]; then
    INITRAMFS=$(readlink -f "$INITRAMFS")
fi

# Verify files exist
if [ ! -f "$KERNEL" ]; then
    echo "ERROR: Kernel not found at $KERNEL"
    echo "Build it: bazel build //vm:extract_kernel"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "ERROR: Initramfs not found at $INITRAMFS"
    echo "Build it: bazel build //vm:build_initramfs"
    exit 1
fi

echo "=== Xibalba Fast QEMU Test ==="
echo "Filesystem: $FILESYSTEM"
echo "Duration: $DURATION seconds"
echo "Kernel: $KERNEL"
echo "Initramfs: $INITRAMFS (includes xibalba binaries)"
echo ""

# Create test disk
TEST_DISK=$(mktemp -u).qcow2
qemu-img create -q -f qcow2 "$TEST_DISK" 5G

# Xibalba binaries are embedded in initramfs - no 9p needed!
echo "✓ Xibalba binaries embedded in initramfs (no network/9p required)"

# Launch QEMU with direct kernel boot
echo ""
echo "Booting VM..."

# Calculate timeout: test duration + 60 seconds for boot/shutdown
TIMEOUT=$((DURATION + 60))
echo "VM timeout: ${TIMEOUT}s (test duration + 60s overhead)"

# Run QEMU with timeout
timeout --foreground --kill-after=10 $TIMEOUT \
    qemu-system-x86_64 \
    -enable-kvm \
    -cpu host \
    -m 2048 \
    -smp 2 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -append "console=ttyS0 rdinit=/init xibalba.fs=$FILESYSTEM xibalba.duration=$DURATION xibalba.readers=$READERS xibalba.writers=$WRITERS xibalba.model=$MODEL" \
    -drive file="$TEST_DISK",if=virtio,format=qcow2 \
    -nographic

VM_EXIT=$?

if [ $VM_EXIT -eq 124 ]; then
    echo ""
    echo "ERROR: VM timed out after ${TIMEOUT}s"
    exit 1
elif [ $VM_EXIT -eq 137 ]; then
    echo ""
    echo "ERROR: VM killed (timeout SIGKILL)"
    exit 1
fi

# Cleanup
rm -f "$TEST_DISK"

echo ""
echo "VM exited with code: $VM_EXIT"
exit $VM_EXIT

