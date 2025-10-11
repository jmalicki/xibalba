#!/bin/bash
# Download and extract kernel from Ubuntu .deb package
# Runs in Docker container (has wget, ar, tar, xz-utils)

set -euo pipefail

if [ $# -ne 1 ]; then
    echo "Usage: $0 OUTPUT_FILE"
    exit 1
fi

OUTPUT_FILE="$1"

# Ubuntu 24.04 LTS kernel package (noble) - using signed kernel
KERNEL_URL="http://archive.ubuntu.com/ubuntu/pool/main/l/linux-signed/linux-image-6.8.0-31-generic_6.8.0-31.31_amd64.deb"

echo "Downloading kernel from $KERNEL_URL..."
cd /tmp
wget -q "$KERNEL_URL" -O kernel.deb

echo "Extracting kernel..."
ar x kernel.deb
# Modern Ubuntu uses zstd compression
if [ -f data.tar.zst ]; then
    tar --zstd -xf data.tar.zst --wildcards './boot/vmlinuz-*' --strip-components=2
elif [ -f data.tar.xz ]; then
    tar -xf data.tar.xz --wildcards './boot/vmlinuz-*' --strip-components=2
else
    echo "ERROR: Unknown data archive format"
    exit 1
fi
cp vmlinuz-* "$OUTPUT_FILE"
# Make readable by all (Docker runs as root, but Bazel needs to read it)
chmod 644 "$OUTPUT_FILE"

echo "✓ Kernel extracted to $OUTPUT_FILE"

