#!/bin/bash
# Run modular chaos test in QEMU VM with eBPF injection
# Uses chaos_test_runner instead of simple_chaos_test

set -euo pipefail

# Parse arguments
WORKLOAD="create_delete"
INJECTOR="none"
FILESYSTEM="btrfs"
DURATION=60
MODEL="posix"

while [[ $# -gt 0 ]]; do
    case $1 in
        --workload)
            WORKLOAD="$2"
            shift 2
            ;;
        --injector)
            INJECTOR="$2"
            shift 2
            ;;
        --filesystem)
            FILESYSTEM="$2"
            shift 2
            ;;
        --duration)
            DURATION="$2"
            shift 2
            ;;
        --model)
            MODEL="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "=== Modular Chaos Test in QEMU VM ==="
echo "Workload:    $WORKLOAD"
echo "Injector:    $INJECTOR"
echo "Filesystem:  $FILESYSTEM"
echo "Duration:    ${DURATION}s"
echo "Model:       $MODEL"
echo

# Find chaos_test_runner binary
RUNNER=""
for path in \
    "bazel-bin/chaos/chaos_test_runner" \
    "chaos_test_runner" \
    "./chaos_test_runner"; do
    if [ -f "$path" ]; then
        RUNNER="$path"
        break
    fi
done

if [ -z "$RUNNER" ]; then
    echo "Error: chaos_test_runner not found"
    exit 1
fi

echo "Using runner: $RUNNER"

# Find kernel and initramfs
KERNEL=""
INITRAMFS=""

for kpath in \
    "bazel-bin/vm/vmlinuz" \
    "vmlinuz" \
    "/boot/vmlinuz-$(uname -r)"; do
    if [ -f "$kpath" ]; then
        KERNEL="$kpath"
        break
    fi
done

for ipath in \
    "bazel-bin/vm/initramfs-modular.cpio.gz" \
    "initramfs-modular.cpio.gz"; do
    if [ -f "$ipath" ]; then
        INITRAMFS="$ipath"
        break
    fi
done

if [ -z "$KERNEL" ]; then
    echo "Error: Kernel not found"
    exit 1
fi

if [ -z "$INITRAMFS" ]; then
    echo "Error: Initramfs not found"
    exit 1
fi

echo "Kernel: $KERNEL"
echo "Initramfs: $INITRAMFS"
echo

# Create temporary directory for test
TEST_MNT="/mnt/test"

# Build kernel command line
# The init script will:
# 1. Mount filesystem
# 2. Run chaos_test_runner with specified parameters
# 3. Report results
# 4. Shutdown
CMDLINE="console=ttyS0 quiet panic=1"
CMDLINE="$CMDLINE xibalba_workload=$WORKLOAD"
CMDLINE="$CMDLINE xibalba_injector=$INJECTOR"
CMDLINE="$CMDLINE xibalba_filesystem=$FILESYSTEM"
CMDLINE="$CMDLINE xibalba_duration=$DURATION"
CMDLINE="$CMDLINE xibalba_model=$MODEL"
CMDLINE="$CMDLINE xibalba_test_dir=$TEST_MNT"

# Launch QEMU
# -nographic: No graphics, use serial console
# -enable-kvm: Use KVM acceleration  
# -m 512M: 512MB RAM (sufficient for chaos testing)
# -kernel: Direct kernel boot
# -initrd: Init RAM filesystem
# -append: Kernel command line
exec qemu-system-x86_64 \
    -nographic \
    -enable-kvm \
    -m 512M \
    -smp 2 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -append "$CMDLINE"

