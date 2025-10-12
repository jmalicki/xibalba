#!/bin/bash
# Download and extract kernel + modules from Ubuntu .deb packages
# Runs in Docker container (has wget, ar, tar, xz-utils, zstd)

set -euo pipefail

if [ $# -ne 2 ]; then
    echo "Usage: $0 OUTPUT_KERNEL OUTPUT_MODULES_DIR"
    exit 1
fi

OUTPUT_FILE="$1"
OUTPUT_MODULES="$2"

KERNEL_VERSION="6.8.0-31-generic"

# Ubuntu 24.04 LTS packages (noble)
KERNEL_IMAGE_URL="http://archive.ubuntu.com/ubuntu/pool/main/l/linux-signed/linux-image-${KERNEL_VERSION}_6.8.0-31.31_amd64.deb"
KERNEL_MODULES_URL="http://archive.ubuntu.com/ubuntu/pool/main/l/linux/linux-modules-${KERNEL_VERSION}_6.8.0-31.31_amd64.deb"

cd /tmp

# Download and extract kernel image
echo "Downloading kernel image..."
wget -q "$KERNEL_IMAGE_URL" -O kernel-image.deb

echo "Extracting kernel..."
ar x kernel-image.deb
if [ -f data.tar.zst ]; then
    tar --zstd -xf data.tar.zst --wildcards './boot/vmlinuz-*' --strip-components=2
elif [ -f data.tar.xz ]; then
    tar -xf data.tar.xz --wildcards './boot/vmlinuz-*' --strip-components=2
else
    echo "ERROR: Unknown data archive format"
    exit 1
fi
cp vmlinuz-* "$OUTPUT_FILE"
chmod 644 "$OUTPUT_FILE"
echo "✓ Kernel extracted to $OUTPUT_FILE"

# Download and extract kernel modules
echo "Downloading kernel modules..."
rm -f *.deb data.tar.* control.tar.*
wget -q "$KERNEL_MODULES_URL" -O kernel-modules.deb

echo "Extracting kernel modules..."
ar x kernel-modules.deb
if [ -f data.tar.zst ]; then
    tar --zstd -xf data.tar.zst
elif [ -f data.tar.xz ]; then
    tar -xf data.tar.xz
else
    echo "ERROR: Unknown data archive format for modules"
    exit 1
fi

# Copy modules to output directory
mkdir -p "$OUTPUT_MODULES"
if [ -d "lib/modules/$KERNEL_VERSION" ]; then
    cp -r "lib/modules/$KERNEL_VERSION" "$OUTPUT_MODULES/"
    echo "✓ Kernel modules extracted to $OUTPUT_MODULES/$KERNEL_VERSION"
    
    # List filesystem modules
    echo "Available filesystem modules:"
    find "$OUTPUT_MODULES/$KERNEL_VERSION" -name "*.ko*" -path "*/fs/*" | sed 's|.*/||' | sort | head -20
else
    echo "⚠️  Kernel modules directory not found"
fi

