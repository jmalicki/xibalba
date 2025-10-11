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

# Check and auto-start libvirt default network
# Assumes user is in libvirt group - fails if not
if command -v virsh &> /dev/null; then
    if ! virsh net-list --all 2>/dev/null | grep -q "default"; then
        echo "✗ libvirt default network not found"
        echo
        echo "ERROR: Default network doesn't exist (should be auto-created by libvirt-daemon-system)"
        echo "Try: sudo systemctl restart libvirtd"
        exit 1
    fi
    
    if ! virsh net-list 2>/dev/null | grep -q "default.*active"; then
        echo "⚠ libvirt default network not active, starting..."
        if ! virsh net-start default 2>&1; then
            echo
            echo "ERROR: Failed to start network. Are you in the libvirt group?"
            echo
            echo "Check: groups | grep libvirt"
            echo "If not: sudo usermod -aG libvirt \$USER"
            echo "Then: Log out and back in"
            exit 1
        fi
    fi
    echo "✓ libvirt default network (active)"
fi

echo

if [ ${#MISSING[@]} -gt 0 ]; then
    echo "❌ Missing dependencies. Install with:"
    echo
    echo "  sudo apt install -y ${MISSING[*]}"
    echo
    echo "Or run the complete setup:"
    echo "  sudo apt install -y libvirt-daemon-system qemu-kvm virtinst cloud-image-utils"
    echo
    echo "Also ensure you're in the correct groups:"
    echo "  sudo usermod -aG libvirt,kvm \$USER"
    echo "  # Then log out and back in"
    echo
    exit 1
fi

echo "✅ All host dependencies satisfied"
exit 0

