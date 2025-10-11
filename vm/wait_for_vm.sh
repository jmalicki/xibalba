#!/bin/bash
set -euo pipefail

# Wait for VM to be SSH-ready
# Usage: wait_for_vm.sh VM_NAME [TIMEOUT_SECONDS]

VM_NAME="${1:-}"
TIMEOUT="${2:-600}"  # 10 minutes default

if [ -z "$VM_NAME" ]; then
    echo "Usage: $0 VM_NAME [TIMEOUT_SECONDS]"
    exit 1
fi

echo "Waiting for VM '$VM_NAME' to be SSH-ready (timeout: ${TIMEOUT}s)..."

# Detect SSH key
SSH_KEY=""
if [ -f "/tmp/xibalba-ssh-keys/id_rsa" ]; then
    SSH_KEY="/tmp/xibalba-ssh-keys/id_rsa"
elif [ -n "${HOME:-}" ] && [ -f "$HOME/.ssh/id_rsa" ]; then
    SSH_KEY="$HOME/.ssh/id_rsa"
fi

# Build SSH options
if [ -n "$SSH_KEY" ]; then
    SSH_OPTS="-i $SSH_KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=2"
else
    SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=2"
fi

ATTEMPTS=$((TIMEOUT / 2))
VM_IP=""

for attempt in $(seq 1 "$ATTEMPTS"); do
    # Try ARP first, fallback to lease
    VM_IP=$(virsh domifaddr "$VM_NAME" --source arp 2>/dev/null | grep -oP '(\d+\.){3}\d+' | head -1 || true)
    if [ -z "$VM_IP" ]; then
        VM_IP=$(virsh domifaddr "$VM_NAME" --source lease 2>/dev/null | grep -oP '(\d+\.){3}\d+' | head -1 || true)
    fi
    
    # Test SSH connectivity
    if [ -n "$VM_IP" ]; then
        # shellcheck disable=SC2086
        if timeout 3 ssh $SSH_OPTS root@"$VM_IP" true 2>/dev/null; then
            echo "✓ VM ready at $VM_IP (after $((attempt * 2))s)"
            echo "$VM_IP" > "/tmp/xibalba-${VM_NAME}-ip.txt"
            exit 0
        fi
        VM_IP=""  # Reset if not ready
    fi
    
    # Show progress every 60 seconds
    remainder=$((attempt % 30))
    if [ "$remainder" -eq 0 ]; then
        echo "  ...still waiting ($((attempt * 2))s / ${TIMEOUT}s)..."
    fi
    
    sleep 2
done

echo "❌ VM not ready after ${TIMEOUT}s"
exit 1

