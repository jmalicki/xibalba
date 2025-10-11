#!/bin/bash
set -euo pipefail

# Destroy a test VM and clean up

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ $# -ne 1 ]; then
    echo "Usage: $0 VM_NAME"
    exit 1
fi

VM_NAME="$1"

echo "=== Destroying VM: $VM_NAME ==="
echo
echo "⚠️  WARNING: This will permanently delete:"
echo "  - VM: $VM_NAME"
echo "  - Disk: $SCRIPT_DIR/images/${VM_NAME}.qcow2"
echo "  - Data disk: $SCRIPT_DIR/images/${VM_NAME}-data.qcow2"
echo
read -p "Are you sure? (type 'yes' to confirm) " -r
echo

if [ "$REPLY" != "yes" ]; then
    echo "Cancelled"
    exit 1
fi

# Check if VM exists
if ! virsh list --all | grep -q "$VM_NAME"; then
    echo "VM $VM_NAME does not exist"
    echo "Available VMs:"
    virsh list --all
    exit 1
fi

# Stop VM if running
if virsh list --name | grep -q "^${VM_NAME}$"; then
    echo "Stopping VM..."
    virsh destroy "$VM_NAME" || true
    sleep 2
    echo "  ✓ Stopped"
fi

# Undefine VM
echo "Undefining VM..."
virsh undefine "$VM_NAME" --remove-all-storage || true
echo "  ✓ Undefined"

# Remove remaining files
echo "Cleaning up files..."
rm -f "$SCRIPT_DIR/images/${VM_NAME}.qcow2"
rm -f "$SCRIPT_DIR/images/${VM_NAME}-data.qcow2"
rm -f "$SCRIPT_DIR/images/${VM_NAME}-cloud-init.iso"
rm -rf "$SCRIPT_DIR/configs/cloud-init-${VM_NAME}"
echo "  ✓ Cleaned up"

echo
echo "✓ VM $VM_NAME destroyed"
echo



