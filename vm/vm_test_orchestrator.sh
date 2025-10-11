#!/bin/bash
set -euo pipefail

# Bazel-native VM Test Orchestrator
# Coordinates VM lifecycle steps with clear error reporting
#
# Usage: vm_test_orchestrator.sh --vm-name NAME --filesystem FS --package PATH

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Parse arguments (handle both --key=value and --key value)
VM_NAME=""
FILESYSTEM=""
PACKAGE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --vm-name=*)
            VM_NAME="${1#*=}"
            shift
            ;;
        --vm-name)
            VM_NAME="$2"
            shift 2
            ;;
        --filesystem=*)
            FILESYSTEM="${1#*=}"
            shift
            ;;
        --filesystem)
            FILESYSTEM="$2"
            shift 2
            ;;
        --package=*)
            PACKAGE="${1#*=}"
            shift
            ;;
        --package)
            PACKAGE="$2"
            shift 2
            ;;
        *)
            # Positional argument (likely the package)
            if [ -z "$PACKAGE" ]; then
                PACKAGE="$1"
            fi
            shift
            ;;
    esac
done

if [ -z "$VM_NAME" ] || [ -z "$FILESYSTEM" ] || [ -z "$PACKAGE" ]; then
    echo "Usage: $0 --vm-name NAME --filesystem FS --package PATH"
    exit 1
fi

echo "════════════════════════════════════════════════════════════════"
echo "  VM Gauntlet Test Orchestrator"
echo "════════════════════════════════════════════════════════════════"
echo "VM: $VM_NAME"
echo "Filesystem: $FILESYSTEM"
echo "Package: $PACKAGE"
echo

# Cleanup on exit
cleanup() {
    local exit_code=$?
    if [ $exit_code -ne 0 ]; then
        echo "❌ Test failed with exit code $exit_code"
    fi
    echo "Cleaning up VM: $VM_NAME"
    "$SCRIPT_DIR/destroy_vm.sh" "$VM_NAME" 2>/dev/null || true
}
trap cleanup EXIT

# Step 1: Create VM
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Step 1: Creating VM"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if ! "$SCRIPT_DIR/create_test_vm.sh" --name "$VM_NAME" --filesystem "$FILESYSTEM"; then
    echo "❌ FAILED: VM creation"
    exit 10
fi
echo "✓ Step 1 complete"
echo

# Step 2: Wait for VM to be SSH-ready
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Step 2: Waiting for VM network and SSH"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if ! "$SCRIPT_DIR/wait_for_vm.sh" "$VM_NAME" 600; then
    echo "❌ FAILED: VM not SSH-ready"
    echo "  Debug: virsh console $VM_NAME"
    exit 20
fi

# Get VM IP
if [ ! -f "/tmp/xibalba-${VM_NAME}-ip.txt" ]; then
    echo "❌ FAILED: Could not determine VM IP"
    exit 21
fi
VM_IP=$(cat "/tmp/xibalba-${VM_NAME}-ip.txt")
echo "✓ Step 2 complete (VM IP: $VM_IP)"
echo

# Step 3: Deploy Xibalba
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Step 3: Deploying Xibalba"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if ! "$SCRIPT_DIR/deploy_xibalba.sh" "$VM_NAME" "$PACKAGE"; then
    echo "❌ FAILED: Xibalba deployment"
    exit 30
fi
echo "✓ Step 3 complete"
echo

# Step 4: Run gauntlet tests
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Step 4: Running Gauntlet Tests"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if ! "$SCRIPT_DIR/run_gauntlet_tests.sh" "$VM_NAME" "$FILESYSTEM"; then
    echo "❌ FAILED: Gauntlet tests"
    exit 40
fi
echo "✓ Step 4 complete"
echo

echo "════════════════════════════════════════════════════════════════"
echo "✓ All tests passed!"
echo "════════════════════════════════════════════════════════════════"

