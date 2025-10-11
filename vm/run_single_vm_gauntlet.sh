#!/bin/bash
set -euo pipefail

# Single VM Gauntlet Test
# This script does ONE thing: test ONE filesystem in ONE VM
# Bazel handles parallelism, not bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Parse args
FILESYSTEM=""
VM_NAME=""
PACKAGE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --filesystem=*)
            FILESYSTEM="${1#*=}"
            shift
            ;;
        --vm-name=*)
            VM_NAME="${1#*=}"
            shift
            ;;
        --*)
            shift
            ;;
        *)
            PACKAGE="$1"
            shift
            ;;
    esac
done

if [ -z "$FILESYSTEM" ] || [ -z "$VM_NAME" ] || [ -z "$PACKAGE" ]; then
    echo "Usage: $0 --filesystem=<fs> --vm-name=<name> <package.deb>"
    exit 1
fi

echo "═══════════════════════════════════════════════════════════════"
echo "  VM Gauntlet: $FILESYSTEM"
echo "═══════════════════════════════════════════════════════════════"
echo "VM: $VM_NAME"
echo "Package: $PACKAGE"
echo

# Detect SSH key
if [ -f "/tmp/xibalba-ssh-keys/id_rsa" ]; then
    SSH_OPTS="-i /tmp/xibalba-ssh-keys/id_rsa -o StrictHostKeyChecking=no -o ConnectTimeout=2"
elif [ -n "${HOME:-}" ] && [ -f "$HOME/.ssh/id_rsa" ]; then
    SSH_OPTS="-i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no -o ConnectTimeout=2"
else
    SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=2"
fi

# Cleanup on exit
# shellcheck disable=SC2317
cleanup() {
    echo "Cleaning up VM: $VM_NAME"
    "$SCRIPT_DIR/destroy_vm.sh" "$VM_NAME" || true
}
trap cleanup EXIT

# Step 1: Create VM
echo "Step 1: Creating VM..."
"$SCRIPT_DIR/create_test_vm.sh" --name "$VM_NAME" --filesystem "$FILESYSTEM"

# Step 2: Get VM IP (hostnames don't resolve in libvirt)
echo "Step 2: Getting VM IP address (may take 2-3 min for DHCP)..."
VM_IP=""
for attempt in {1..90}; do
    VM_IP=$(virsh domifaddr "$VM_NAME" --source lease 2>/dev/null | grep -oP '(\d+\.){3}\d+' | head -1)
    if [ -n "$VM_IP" ]; then
        echo "  ✓ VM IP: $VM_IP (attempt $attempt)"
        break
    fi
    sleep 2
done

if [ -z "$VM_IP" ]; then
    echo "❌ Failed to get VM IP address after 3 minutes"
    echo "   Try: virsh domifaddr $VM_NAME --source lease"
    echo "   Or:  virsh console $VM_NAME"
    exit 1
fi

# Step 3: Wait for SSH
echo "Step 3: Waiting for SSH..."
# shellcheck disable=SC2086
timeout 120 bash -c "until ssh $SSH_OPTS root@$VM_IP true 2>/dev/null; do sleep 2; done" || {
    echo "❌ Failed to connect to VM at $VM_IP"
    exit 1
}

# Step 4: Deploy package
echo "Step 4: Deploying Xibalba..."
# shellcheck disable=SC2086
scp $SSH_OPTS "$PACKAGE" "root@${VM_IP}:/tmp/" || exit 1
# shellcheck disable=SC2086,SC2029
ssh $SSH_OPTS "root@${VM_IP}" "apt update && apt install -y /tmp/$(basename "$PACKAGE")" || exit 1

# Step 5: Run gauntlet
echo "Step 5: Running progressive gauntlet..."
echo "  1. EVENTUAL (baseline) - should PASS"
echo "  2. WEAK (POSIX) - should PASS"  
echo "  3. STRICT (ideal) - quantify departures"
echo

# shellcheck disable=SC2086,SC2029
if ssh $SSH_OPTS "root@${VM_IP}" \
    "XIBALBA_DURATION=${XIBALBA_DURATION:-300} XIBALBA_READERS=${XIBALBA_READERS:-10} XIBALBA_WRITERS=${XIBALBA_WRITERS:-3} xibalba-gauntlet $FILESYSTEM"; then
    echo "✅ Gauntlet PASSED"
    
    # Collect results to Bazel's test output directory
    if [ -n "${TEST_UNDECLARED_OUTPUTS_DIR:-}" ]; then
        # shellcheck disable=SC2086
        ssh $SSH_OPTS "root@${VM_IP}" "cat /var/log/xibalba/gauntlet/latest-gauntlet.json" \
            > "$TEST_UNDECLARED_OUTPUTS_DIR/${VM_NAME}-gauntlet.json" 2>/dev/null || true
        # shellcheck disable=SC2086
        ssh $SSH_OPTS "root@${VM_IP}" "cat /var/log/xibalba/gauntlet/latest-gauntlet.txt" \
            > "$TEST_UNDECLARED_OUTPUTS_DIR/${VM_NAME}-gauntlet.txt" 2>/dev/null || true
    fi
    
    exit 0
else
    echo "❌ Gauntlet FAILED"
    
    # Collect failure artifacts
    if [ -n "${TEST_UNDECLARED_OUTPUTS_DIR:-}" ]; then
        # shellcheck disable=SC2086
        ssh $SSH_OPTS "root@${VM_IP}" "cat /var/log/xibalba/gauntlet/latest-gauntlet.json" \
            > "$TEST_UNDECLARED_OUTPUTS_DIR/${VM_NAME}-gauntlet.json" 2>/dev/null || true
        # shellcheck disable=SC2086
        ssh $SSH_OPTS "root@${VM_IP}" "cat /var/log/xibalba/gauntlet/latest-gauntlet.txt" \
            > "$TEST_UNDECLARED_OUTPUTS_DIR/${VM_NAME}-gauntlet.txt" 2>/dev/null || true
    fi
    
    exit 1
fi

