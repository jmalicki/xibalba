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
cleanup() {
    echo "Cleaning up VM: $VM_NAME"
    "$SCRIPT_DIR/destroy_vm.sh" "$VM_NAME" || true
}
trap cleanup EXIT

# Step 1: Create VM
echo "Step 1: Creating VM..."
"$SCRIPT_DIR/create_test_vm.sh" --name "$VM_NAME" --filesystem "$FILESYSTEM"

# Step 2: Wait for SSH
echo "Step 2: Waiting for SSH..."
timeout 120 bash -c "until ssh $SSH_OPTS root@$VM_NAME true 2>/dev/null; do sleep 2; done" || {
    echo "❌ Failed to connect to VM"
    exit 1
}

# Step 3: Deploy package
echo "Step 3: Deploying Xibalba..."
scp $SSH_OPTS "$PACKAGE" "root@${VM_NAME}:/tmp/" || exit 1
ssh $SSH_OPTS "root@${VM_NAME}" "apt update && apt install -y /tmp/$(basename $PACKAGE)" || exit 1

# Step 4: Run gauntlet
echo "Step 4: Running progressive gauntlet..."
echo "  1. EVENTUAL (baseline) - should PASS"
echo "  2. WEAK (POSIX) - should PASS"  
echo "  3. STRICT (ideal) - quantify departures"
echo

if ssh $SSH_OPTS "root@${VM_NAME}" \
    "XIBALBA_DURATION=${XIBALBA_DURATION:-300} XIBALBA_READERS=${XIBALBA_READERS:-10} XIBALBA_WRITERS=${XIBALBA_WRITERS:-3} xibalba-gauntlet $FILESYSTEM"; then
    echo "✅ Gauntlet PASSED"
    
    # Collect results to Bazel's test output directory
    if [ -n "${TEST_UNDECLARED_OUTPUTS_DIR:-}" ]; then
        ssh $SSH_OPTS "root@${VM_NAME}" "cat /var/log/xibalba/gauntlet/latest-gauntlet.json" \
            > "$TEST_UNDECLARED_OUTPUTS_DIR/${VM_NAME}-gauntlet.json" 2>/dev/null || true
        ssh $SSH_OPTS "root@${VM_NAME}" "cat /var/log/xibalba/gauntlet/latest-gauntlet.txt" \
            > "$TEST_UNDECLARED_OUTPUTS_DIR/${VM_NAME}-gauntlet.txt" 2>/dev/null || true
    fi
    
    exit 0
else
    echo "❌ Gauntlet FAILED"
    
    # Collect failure artifacts
    if [ -n "${TEST_UNDECLARED_OUTPUTS_DIR:-}" ]; then
        ssh $SSH_OPTS "root@${VM_NAME}" "cat /var/log/xibalba/gauntlet/latest-gauntlet.json" \
            > "$TEST_UNDECLARED_OUTPUTS_DIR/${VM_NAME}-gauntlet.json" 2>/dev/null || true
        ssh $SSH_OPTS "root@${VM_NAME}" "cat /var/log/xibalba/gauntlet/latest-gauntlet.txt" \
            > "$TEST_UNDECLARED_OUTPUTS_DIR/${VM_NAME}-gauntlet.txt" 2>/dev/null || true
    fi
    
    exit 1
fi

