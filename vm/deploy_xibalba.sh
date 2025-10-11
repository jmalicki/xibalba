#!/bin/bash
set -euo pipefail

# Deploy Xibalba binaries to a test VM

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ $# -ne 1 ]; then
    echo "Usage: $0 VM_NAME"
    exit 1
fi

VM_NAME="$1"

echo "=== Deploying Xibalba to $VM_NAME ==="
echo

# Check if VM exists and is running
if ! virsh list --name | grep -q "^${VM_NAME}$"; then
    echo "ERROR: VM $VM_NAME is not running"
    echo "Start with: virsh start $VM_NAME"
    exit 1
fi

# Get VM IP
echo "Getting VM IP..."
VM_IP=$(virsh domifaddr "$VM_NAME" 2>/dev/null | grep -oP '(\d+\.){3}\d+' | head -1 || true)
if [ -z "$VM_IP" ]; then
    echo "ERROR: Could not determine VM IP"
    echo "Try: virsh domifaddr $VM_NAME"
    exit 1
fi
echo "  ✓ VM IP: $VM_IP"

# Build Xibalba locally
echo "Building Xibalba..."
cd "$PROJECT_ROOT"
if [ ! -f "./compile.sh" ]; then
    echo "ERROR: compile.sh not found in $PROJECT_ROOT"
    exit 1
fi
./compile.sh
echo "  ✓ Built successfully"

# Check binaries exist
if [ ! -d "./bin" ] || [ ! -f "./bin/pause_controller" ]; then
    echo "ERROR: Binaries not found in ./bin/"
    exit 1
fi

# Create remote directory
echo "Creating remote directory..."
ssh -o StrictHostKeyChecking=no root@"$VM_IP" "mkdir -p /root/xibalba/bin"
echo "  ✓ Created /root/xibalba/bin"

# Copy binaries
echo "Copying binaries..."
scp -o StrictHostKeyChecking=no -r ./bin/* root@"$VM_IP":/root/xibalba/bin/
echo "  ✓ Copied binaries"

# Verify on remote
echo "Verifying deployment..."
ssh root@"$VM_IP" bash << 'REMOTE_VERIFY'
set -e
cd /root/xibalba/bin
echo "Files:"
ls -lh
echo
echo "Testing eBPF load (may show warnings, that's OK)..."
timeout 2 ./pause_controller 10 100 || true
echo
echo "✓ Deployment verified"
REMOTE_VERIFY

echo
echo "=== Deployment Complete ==="
echo
echo "Deployed to: root@$VM_IP:/root/xibalba/"
echo
echo "Next steps:"
echo "  # SSH to VM"
echo "  ssh root@$VM_IP"
echo
echo "  # Inside VM, run tests"
echo "  cd /root/xibalba/bin"
echo "  ./pause_controller 50 500 &"
echo "  ./simple_chaos_test /test/xibalba_test"
echo
echo "Or use automation:"
echo "  $SCRIPT_DIR/run_tests.sh $VM_NAME"
echo



