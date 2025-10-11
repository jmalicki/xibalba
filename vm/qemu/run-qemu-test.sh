#!/bin/bash
# Launch QEMU VM for Xibalba testing
# Uses direct kernel boot for maximum speed

set -euo pipefail

# Arguments
FILESYSTEM=${1:-ext4}
DURATION=${2:-300}
READERS=${3:-10}
WRITERS=${4:-3}

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
qemu-system-x86_64 \
    -enable-kvm \
    -cpu host \
    -m 2048 \
    -smp 2 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -append "console=ttyS0 rdinit=/init xibalba.fs=$FILESYSTEM xibalba.duration=$DURATION xibalba.readers=$READERS xibalba.writers=$WRITERS" \
    -drive file="$TEST_DISK",if=virtio,format=qcow2 \
    -nographic

VM_EXIT=$?

# Cleanup
rm -f "$TEST_DISK"

echo ""
echo "VM exited with code: $VM_EXIT"
exit $VM_EXIT

