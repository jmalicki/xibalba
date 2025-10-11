#!/bin/bash
# Verify libvirt network is ready for VM creation
# This is a READ-ONLY check - does not modify system state
set -euo pipefail

# Always use system libvirt (not session)
VIRSH="virsh --connect qemu:///system"

echo "Verifying libvirt network..."
echo

# Check if default network exists
if ! $VIRSH net-list --all 2>/dev/null | grep -q "default"; then
    echo "✗ libvirt default network not found"
    echo
    echo "ERROR: Default network doesn't exist."
    echo "This should be auto-created by libvirt-daemon-system."
    echo
    echo "Fix:"
    echo "  sudo systemctl restart libvirtd"
    exit 1
fi

# Check if it's active
if $VIRSH net-list 2>/dev/null | grep -q "default.*active"; then
    echo "✓ libvirt default network is active"
elif $VIRSH net-list --all 2>/dev/null | grep "default" | grep -q "inactive"; then
    echo "✗ libvirt default network is inactive"
    echo
    echo "ERROR: Network must be started before creating VMs."
    echo
    echo "Fix:"
    echo "  virsh --connect qemu:///system net-start default"
    echo "  virsh --connect qemu:///system net-autostart default"
    echo
    echo "Or let libvirtd manage it:"
    echo "  sudo systemctl restart libvirtd"
    exit 1
else
    # Unexpected state - show debug info
    echo "⚠ Unexpected network state:"
    $VIRSH net-list --all 2>&1
    exit 1
fi

echo
echo "✅ Network is ready for VM creation"
exit 0

