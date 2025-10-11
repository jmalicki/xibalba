#!/bin/bash
# Verify required VM tools are installed
set -euo pipefail

echo "Checking VM tools..."

MISSING=()

check_tool() {
    local tool=$1
    local package=$2
    
    if command -v "$tool" &> /dev/null; then
        echo "✓ $tool"
        return 0
    else
        echo "✗ $tool (install: $package)"
        MISSING+=("$package")
        return 1
    fi
}

check_tool virsh "libvirt-daemon-system libvirt-clients"
check_tool virt-install "virtinst"
check_tool cloud-localds "cloud-image-utils"
check_tool qemu-system-x86_64 "qemu-system-x86"

if [ ${#MISSING[@]} -gt 0 ]; then
    echo
    echo "❌ Missing tools. Install with:"
    echo "  sudo apt install -y ${MISSING[*]}"
    exit 1
fi

echo "✅ All tools installed"
exit 0

