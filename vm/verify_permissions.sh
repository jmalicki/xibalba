#!/bin/bash
# Verify user has correct group membership for VM operations
set -euo pipefail

echo "Checking group permissions..."

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
    exit 1
fi

echo "✓ User in libvirt group"

if ! groups 2>/dev/null | grep -q kvm; then
    echo "⚠ User not in kvm group (KVM acceleration unavailable)"
    echo "  Recommended: sudo usermod -aG kvm \$USER"
else
    echo "✓ User in kvm group"
fi

echo "✅ Permissions OK"
exit 0

