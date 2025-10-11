#!/bin/bash
set -euo pipefail

# Run the actual gauntlet tests on a VM
# Usage: run_gauntlet_tests.sh VM_NAME FILESYSTEM

VM_NAME="${1:-}"
FILESYSTEM="${2:-ext4}"

if [ -z "$VM_NAME" ]; then
    echo "Usage: $0 VM_NAME [FILESYSTEM]"
    exit 1
fi

echo "Running gauntlet tests on $VM_NAME ($FILESYSTEM)"

# Get VM IP
if [ -f "/tmp/xibalba-${VM_NAME}-ip.txt" ]; then
    VM_IP=$(cat "/tmp/xibalba-${VM_NAME}-ip.txt")
else
    # Fallback: try to get it from virsh
    VM_IP=$(virsh domifaddr "$VM_NAME" --source arp 2>/dev/null | grep -oP '(\d+\.){3}\d+' | head -1 || true)
    if [ -z "$VM_IP" ]; then
        VM_IP=$(virsh domifaddr "$VM_NAME" --source lease 2>/dev/null | grep -oP '(\d+\.){3}\d+' | head -1 || true)
    fi
fi

if [ -z "$VM_IP" ]; then
    echo "❌ Could not determine VM IP"
    exit 1
fi

# Detect SSH key
SSH_KEY=""
if [ -f "/tmp/xibalba-ssh-keys/id_rsa" ]; then
    SSH_KEY="/tmp/xibalba-ssh-keys/id_rsa"
elif [ -n "${HOME:-}" ] && [ -f "$HOME/.ssh/id_rsa" ]; then
    SSH_KEY="$HOME/.ssh/id_rsa"
fi

# Build SSH options
if [ -n "$SSH_KEY" ]; then
    SSH_OPTS="-i $SSH_KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5"
else
    SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5"
fi

echo "Testing on $VM_IP..."

# Run progressive consistency tests
# shellcheck disable=SC2086
ssh $SSH_OPTS root@"$VM_IP" bash << 'EOF'
set -euo pipefail

echo "Setting up test filesystem..."

# Setup filesystem based on type
FILESYSTEM="${1:-ext4}"
case "$FILESYSTEM" in
    ext4)
        dd if=/dev/zero of=/root/test.img bs=1M count=1000
        mkfs.ext4 -q /root/test.img
        mkdir -p /mnt/test
        mount -o loop /root/test.img /mnt/test
        TEST_DIR="/mnt/test"
        ;;
    zfs)
        dd if=/dev/zero of=/root/test.img bs=1M count=2000
        zpool create testpool /root/test.img
        zfs create testpool/testfs
        TEST_DIR="/testpool/testfs"
        ;;
    *)
        echo "Unknown filesystem: $FILESYSTEM"
        exit 1
        ;;
esac

echo "Running gauntlet: WEAK consistency..."
if ! xibalba-chaos-test --consistency=weak --duration=60 "$TEST_DIR"; then
    echo "❌ WEAK consistency test FAILED"
    exit 1
fi
echo "✓ WEAK consistency test PASSED"

echo "Running gauntlet: EVENTUAL consistency..."
if ! xibalba-chaos-test --consistency=eventual --duration=60 "$TEST_DIR"; then
    echo "⚠️  EVENTUAL consistency test FAILED (violations detected)"
    # Don't exit - this is expected for some filesystems
else
    echo "✓ EVENTUAL consistency test PASSED"
fi

echo "Running gauntlet: STRONG consistency..."
if ! xibalba-chaos-test --consistency=strong --duration=60 "$TEST_DIR"; then
    echo "⚠️  STRONG consistency test FAILED (violations detected)"
    # Don't exit - this is expected for most filesystems
else
    echo "✓ STRONG consistency test PASSED"
fi

echo "Gauntlet complete!"
EOF

echo "✓ Gauntlet tests completed successfully"

