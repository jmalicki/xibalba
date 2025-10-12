#!/bin/bash
# Build minimal initramfs with filesystem tools and custom init
# Runs in Docker container (has dpkg-deb, cpio, gzip, busybox)

set -euo pipefail

if [ $# -lt 5 ]; then
    echo "Usage: $0 INIT_SCRIPT PAUSE_CONTROLLER SIMPLE_CHAOS_TEST GAUNTLET_SCRIPT OUTPUT_FILE [KERNEL_MODULES_TAR]"
    exit 1
fi

INIT_SCRIPT="$1"
PAUSE_CONTROLLER="$2"
SIMPLE_CHAOS_TEST="$3"
GAUNTLET_SCRIPT="$4"
OUTPUT_FILE="$5"
KERNEL_MODULES_TAR="${6:-}"

echo "Building minimal initramfs with embedded xibalba binaries and kernel modules..."

cd /tmp
mkdir -p initrd/{bin,sbin,usr/bin,usr/sbin,dev,proc,sys,test,opt/xibalba}

# Install busybox, bash, and jq
echo "Installing busybox, bash, and jq..."
apt-get install -y -qq busybox-static bash jq
cp /bin/busybox initrd/bin/busybox
cp /bin/bash initrd/bin/bash
cp /usr/bin/jq initrd/usr/bin/jq

# Create busybox symlinks for essential commands
cd initrd/bin
for cmd in sh ash mount umount mkdir cat grep echo cut modprobe lsmod date sleep kill timeout rm mv cp ls pwd chmod chown dd seq; do
    ln -sf busybox "$cmd"
done
cd ../..

# Install filesystem tools (mkfs.ext4, mkfs.xfs, mkfs.btrfs, zfs)
echo "Installing filesystem tools..."
apt-get install -y -qq e2fsprogs xfsprogs btrfs-progs zfsutils-linux kmod

# Copy mkfs tools to initrd
echo "Copying filesystem utilities..."
cp /sbin/mkfs.ext4 /sbin/mke2fs initrd/sbin/ || true
cp /sbin/mkfs.xfs initrd/sbin/ || true
cp /sbin/mkfs.btrfs initrd/sbin/ || true

# Copy ZFS tools
cp /sbin/zfs /sbin/zpool initrd/sbin/ || true

# Copy modprobe (needed for ZFS kernel modules)
cp /sbin/modprobe /sbin/insmod /sbin/rmmod initrd/sbin/ || true
cp /sbin/depmod initrd/sbin/ || true

# Copy xibalba binaries into initramfs FIRST
echo "Installing xibalba binaries..."
mkdir -p initrd/usr/bin
cp "$PAUSE_CONTROLLER" initrd/usr/bin/pause_controller
cp "$SIMPLE_CHAOS_TEST" initrd/usr/bin/simple_chaos_test
cp "$GAUNTLET_SCRIPT" initrd/usr/bin/xibalba-gauntlet
chmod +x initrd/usr/bin/*

# Copy required libraries AFTER binaries are in place
echo "Copying required libraries..."
mkdir -p initrd/lib/x86_64-linux-gnu initrd/lib64

# Copy libraries for bash, jq, xibalba binaries, mkfs tools, zfs tools, and modprobe
for binary in initrd/bin/bash initrd/usr/bin/jq initrd/usr/bin/pause_controller initrd/usr/bin/simple_chaos_test initrd/sbin/mkfs.* initrd/sbin/zfs initrd/sbin/zpool initrd/sbin/modprobe; do
    if [ -f "$binary" ]; then
        echo "  Copying libs for $(basename $binary)..."
        # Get list of libraries first, then copy (avoid subshell issues)
        LIBS=$(ldd "$binary" 2>/dev/null | grep "=> /" | awk '{print $3}')
        for lib in $LIBS; do
            if [ -f "$lib" ]; then
                cp -L "$lib" initrd/lib/x86_64-linux-gnu/ 2>/dev/null || true
            fi
        done
    fi
done

# Add ld-linux linker
cp -L /lib64/ld-linux-x86-64.so.2 initrd/lib64/

# Extract kernel modules from provided tarball
if [ -n "$KERNEL_MODULES_TAR" ] && [ -f "$KERNEL_MODULES_TAR" ]; then
    echo "Extracting kernel modules from $KERNEL_MODULES_TAR..."
    mkdir -p initrd/lib/modules
    tar -xzf "$KERNEL_MODULES_TAR" -C initrd/lib/modules
    
    KERNEL_VERSION=$(ls initrd/lib/modules/ | head -1)
    if [ -n "$KERNEL_VERSION" ]; then
        echo "  ✓ Kernel modules extracted (version: $KERNEL_VERSION)"
        
        # List available filesystem modules
        echo "  Available filesystem modules:"
        find initrd/lib/modules/$KERNEL_VERSION -path "*/fs/*.ko*" -type f 2>/dev/null | \
            sed 's|.*/||' | sed 's|\.ko.*||' | sort | head -10 | sed 's/^/    - /'
        
        # Count total modules
        MODULE_COUNT=$(find initrd/lib/modules/$KERNEL_VERSION -name "*.ko*" | wc -l)
        echo "  Total modules: $MODULE_COUNT"
    else
        echo "  ⚠️  Kernel modules extracted but version unknown"
    fi
else
    echo "  ⚠️  No kernel modules provided (filesystems may be built-in or unavailable)"
fi

# Copy our custom init script
echo "Installing custom init..."
cp "$INIT_SCRIPT" initrd/init
chmod +x initrd/init

# Skip device nodes - devtmpfs will create them when we mount
# (mknod requires CAP_MKNOD which Docker doesn't have by default)

# Create initramfs
echo "Creating initramfs archive..."
if [ ! -d "initrd" ]; then
    echo "ERROR: initrd directory not found!"
    exit 1
fi
cd initrd
find . | cpio -o -H newc 2>/dev/null | gzip -9 > "$OUTPUT_FILE"

if [ ! -f "$OUTPUT_FILE" ]; then
    echo "ERROR: Failed to create $OUTPUT_FILE"
    exit 1
fi

echo "✓ Initramfs created at $OUTPUT_FILE"
SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
FILES=$(find . | wc -l)
echo "  Size: $SIZE ($FILES files)"

