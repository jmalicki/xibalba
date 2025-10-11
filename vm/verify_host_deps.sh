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

# Check libvirt permissions
if ! groups 2>/dev/null | grep -q libvirt; then
    echo "✗ User not in libvirt group"
    echo
    echo "ERROR: You must be in the 'libvirt' group to run VM tests"
    echo
    echo "Fix:"
    echo "  sudo usermod -aG libvirt,kvm \$USER"
    echo "  # Then log out and back in"
    echo
    echo "Verify after login:"
    echo "  groups | grep libvirt"
    echo
    exit 1
fi
echo "✓ User in libvirt group"

# Check and auto-start libvirt default network
if command -v virsh &> /dev/null; then
    if ! virsh net-list --all 2>/dev/null | grep -q "default"; then
        echo "✗ libvirt default network not found"
        echo
        echo "ERROR: Default network doesn't exist"
        echo "This should be auto-created by libvirt-daemon-system."
        echo
        echo "Fix: sudo systemctl restart libvirtd"
        exit 1
    fi
    
    if ! virsh net-list 2>/dev/null | grep -q "default.*active"; then
        echo "⚠ Default network inactive, starting..."
        # User is in libvirt group, so this should work
        if ! virsh net-start default 2>&1; then
            echo
            echo "ERROR: Failed to start libvirt default network"
            echo
            echo "This usually means:"
            echo "  1. You're not actually in the libvirt group (group change requires re-login)"
            echo "  2. libvirtd is not running"
            echo
            echo "Fix:"
            echo "  groups | grep libvirt  # Should show libvirt"
            echo "  sudo systemctl status libvirtd  # Should be active"
            echo
            echo "If not in group: log out and back in after running:"
            echo "  sudo usermod -aG libvirt,kvm \$USER"
            exit 1
        fi
        echo "✓ libvirt default network (started)"
    else
        echo "✓ libvirt default network (active)"
    fi
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

