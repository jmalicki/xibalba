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

# Check for libvirt default network in user session
SETUP_NEEDED=()
if command -v virsh &> /dev/null; then
    if virsh --connect qemu:///session net-list --all 2>/dev/null | grep -q "default"; then
        if virsh --connect qemu:///session net-list 2>/dev/null | grep -q "default.*active"; then
            echo "✓ libvirt default network (active)"
        else
            echo "⚠ libvirt default network (not started)"
            SETUP_NEEDED+=("start-network")
        fi
    else
        echo "✗ libvirt default network (not configured)"
        SETUP_NEEDED+=("create-network")
    fi
fi

echo

if [ ${#MISSING[@]} -eq 0 ] && [ ${#SETUP_NEEDED[@]} -eq 0 ]; then
    echo "✅ All host dependencies satisfied"
    exit 0
fi

if [ ${#MISSING[@]} -gt 0 ]; then
    echo "❌ Missing dependencies. Install with:"
    echo
    echo "  sudo apt install -y ${MISSING[*]}"
    echo
    echo "Or run the complete setup:"
    echo "  sudo apt install -y libvirt-daemon-system qemu-kvm virtinst cloud-image-utils"
    echo
fi

if [ ${#SETUP_NEEDED[@]} -gt 0 ]; then
    echo "⚠️  Network setup needed:"
    echo
    for item in "${SETUP_NEEDED[@]}"; do
        case $item in
            create-network)
                echo "Create libvirt default network:"
                echo "  virsh --connect qemu:///session net-define /dev/stdin <<'EOF'"
                echo "  <network>"
                echo "    <name>default</name>"
                echo "    <forward mode='nat'/>"
                echo "    <bridge name='virbr1' stp='on' delay='0'/>"
                echo "    <ip address='192.168.124.1' netmask='255.255.255.0'>"
                echo "      <dhcp>"
                echo "        <range start='192.168.124.2' end='192.168.124.254'/>"
                echo "      </dhcp>"
                echo "    </ip>"
                echo "  </network>"
                echo "  EOF"
                echo "  virsh --connect qemu:///session net-start default"
                echo "  virsh --connect qemu:///session net-autostart default"
                echo
                ;;
            start-network)
                echo "Start libvirt default network:"
                echo "  virsh --connect qemu:///session net-start default"
                echo
                ;;
        esac
    done
fi

exit 1

