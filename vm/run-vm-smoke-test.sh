#!/bin/bash
# Quick VM infrastructure smoke test
# Validates VMs boot and tests run without full 15-minute gauntlet

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Get package path from argument
PACKAGE_PATH="${1:-}"
if [ -z "$PACKAGE_PATH" ]; then
    echo "ERROR: Package path required"
    echo "Usage: $0 <path-to-xibalba.deb>"
    exit 1
fi

# Configuration
VM_NAME="${VM_NAME:-xibalba-smoke}"
FILESYSTEM="${FILESYSTEM:-tmpfs}"
TEST_DURATION="${TEST_DURATION:-30}"
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
RESULTS_DIR="${PROJECT_ROOT}/test-results/vm-smoke-${TIMESTAMP}"

echo "════════════════════════════════════════════════════════════════"
echo "    Xibalba VM Infrastructure Smoke Test"
echo "════════════════════════════════════════════════════════════════"
echo
echo "Fast validation (~2 minutes):"
echo "  VM: $VM_NAME"
echo "  Filesystem: $FILESYSTEM"
echo "  Duration: ${TEST_DURATION}s"
echo
echo "Results: $RESULTS_DIR"
echo "════════════════════════════════════════════════════════════════"
echo

mkdir -p "$RESULTS_DIR"

# Check if VM exists
if ! virsh list --all | grep -q "$VM_NAME"; then
    echo "Creating VM..."
    "$SCRIPT_DIR/create_test_vm.sh" --name "$VM_NAME" --ram 1024 --disk 5
else
    echo "Starting existing VM..."
    virsh start "$VM_NAME" 2>/dev/null || true
fi

# Wait for VM to boot
echo "Waiting for VM to boot..."
MAX_WAIT=120
for i in $(seq 1 $MAX_WAIT); do
    if ssh -o StrictHostKeyChecking=no -o ConnectTimeout=2 "root@$VM_NAME" echo "ready" 2>/dev/null; then
        echo "✓ VM is ready"
        break
    fi
    if [ "$i" -eq "$MAX_WAIT" ]; then
        echo "ERROR: VM failed to boot within ${MAX_WAIT}s"
        exit 1
    fi
    sleep 1
done

# Deploy package
echo "Deploying xibalba package..."
"$SCRIPT_DIR/deploy_xibalba.sh" "$VM_NAME" "$PACKAGE_PATH"

# Run smoke test
echo
echo "Running smoke test..."
# shellcheck disable=SC2029  # Intentionally expand on client side
ssh "root@$VM_NAME" "FILESYSTEMS=$FILESYSTEM TEST_DURATION=$TEST_DURATION xibalba-smoke-test" | tee "$RESULTS_DIR/smoke-test.log"

# Check result
if ssh "root@$VM_NAME" "[ -f /var/log/xibalba-smoke.log ]"; then
    scp "root@$VM_NAME:/var/log/xibalba-smoke.log" "$RESULTS_DIR/" || true
fi

echo
echo "════════════════════════════════════════════════════════════════"
echo "  Smoke Test Complete"
echo "════════════════════════════════════════════════════════════════"
echo
echo "✅ Infrastructure validation successful!"
echo "   Results: $RESULTS_DIR"
echo

exit 0

