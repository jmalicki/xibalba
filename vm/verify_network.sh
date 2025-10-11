#!/bin/bash
# Verify libvirt network is configured and active
# Auto-starts if inactive (requires libvirt group membership)
set -euo pipefail

echo "Checking libvirt network..."

if ! virsh net-list --all 2>/dev/null | grep -q "default"; then
    echo "✗ Default network not found"
    echo
    echo "ERROR: libvirt default network doesn't exist"
    echo "This should be auto-created by libvirt-daemon-system."
    echo
    echo "Fix: sudo systemctl restart libvirtd"
    exit 1
fi

if ! virsh net-list 2>/dev/null | grep -q "default.*active"; then
    echo "⚠ Default network inactive, starting..."
    if ! virsh net-start default 2>&1; then
        echo
        echo "ERROR: Failed to start network"
        echo
        echo "Possible causes:"
        echo "  - Not in libvirt group (check: groups | grep libvirt)"
        echo "  - Need to log out/in after adding group"
        echo "  - libvirtd not running (check: systemctl status libvirtd)"
        exit 1
    fi
    echo "✓ Default network (started)"
else
    echo "✓ Default network (active)"
fi

echo "✅ Network ready"
exit 0

