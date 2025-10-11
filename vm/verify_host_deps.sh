#!/bin/bash
# Bazel-integrated prerequisite verification
# Returns 0 if all dependencies met, 1 otherwise
# Designed to be called as a Bazel test

set -euo pipefail

MISSING=()

echo "Verifying Xibalba VM host dependencies..."
echo

# Check required tools
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

echo

if [ ${#MISSING[@]} -eq 0 ]; then
    echo "✅ All host dependencies satisfied"
    exit 0
else
    echo "❌ Missing dependencies. Install with:"
    echo
    echo "  sudo apt install -y ${MISSING[*]}"
    echo
    echo "Or run the complete setup:"
    echo "  sudo apt install -y libvirt-daemon-system qemu-kvm virtinst cloud-image-utils"
    echo
    exit 1
fi

