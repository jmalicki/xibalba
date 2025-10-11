#!/bin/bash
# Ensure libvirt network is ready (IDEMPOTENT)
# Starts network if inactive, succeeds if already active
set -euo pipefail

# Always use system libvirt (not session)
VIRSH="virsh --connect qemu:///system"

echo "Ensuring libvirt network is active..."
echo

# Check if default network exists
if ! $VIRSH net-list --all 2>/dev/null | grep -q "default"; then
    echo "✗ Default network not found"
    echo "ERROR: libvirt-daemon-system should create it automatically"
    echo "Fix: sudo systemctl restart libvirtd"
    exit 1
fi

# Ensure it's active (idempotent)
if $VIRSH net-list 2>/dev/null | grep -q "default.*active"; then
    echo "✓ Network already active"
else
    echo "⚠ Network inactive, starting..."
    if $VIRSH net-start default 2>&1; then
        echo "✓ Network started successfully"
    else
        # Check if it's actually already active (race condition)
        if $VIRSH net-list 2>/dev/null | grep -q "default.*active"; then
            echo "✓ Network is now active (started by another process)"
        else
            echo "❌ Failed to start network"
            echo
            echo "Ensure you're in libvirt group:"
            echo "  groups | grep libvirt"
            echo
            echo "If not, run and re-login:"
            echo "  sudo usermod -aG libvirt \$USER"
            exit 1
        fi
    fi
fi

# Enable autostart for future reboots
if ! $VIRSH net-info default 2>/dev/null | grep -q "Autostart:.*yes"; then
    echo "Enabling autostart..."
    $VIRSH net-autostart default 2>/dev/null || true
fi

echo
echo "✅ Network is ready (idempotent success)"
exit 0

