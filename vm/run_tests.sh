#!/bin/bash
set -euo pipefail

# Run RUDRA tests in a VM

AUTOMATED=false
VM_NAME=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --automated)
            AUTOMATED=true
            shift
            ;;
        *)
            VM_NAME="$1"
            shift
            ;;
    esac
done

if [ -z "$VM_NAME" ]; then
    echo "Usage: $0 [--automated] VM_NAME"
    exit 1
fi

echo "=== Running RUDRA Tests on $VM_NAME ==="
echo

# Check if VM is running
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
    exit 1
fi
echo "  ✓ VM IP: $VM_IP"

# Run tests
echo
echo "=== Running Tests ==="
echo

if [ "$AUTOMATED" = true ]; then
    # Automated mode: run and collect results
    ssh root@"$VM_IP" bash << 'REMOTE_TEST'
set -e
cd /root/rudra/bin

# Create test directory
mkdir -p /test/rudra_test
cd /test/rudra_test
for i in {1..1000}; do
    touch "file_$i.txt"
done
cd /root/rudra/bin

echo "=== Baseline Test (No Chaos) ==="
./simple_chaos_test /test/rudra_test
echo

echo "=== Chaos Test (50% Error Injection) ==="
# Start error injector in background
./pause_controller 50 500 > /tmp/pause_controller.log 2>&1 &
PAUSE_PID=$!
sleep 2

# Run test
./simple_chaos_test /test/rudra_test

# Stop error injector
kill $PAUSE_PID || true
wait $PAUSE_PID || true

echo
echo "=== Error Injector Stats ==="
cat /tmp/pause_controller.log | tail -20

echo
echo "=== Test Complete ==="
REMOTE_TEST

else
    # Interactive mode: SSH to VM
    echo "Connecting to VM..."
    echo "Run these commands inside the VM:"
    echo
    echo "  cd /root/rudra/bin"
    echo "  ./pause_controller 50 500 &"
    echo "  ./simple_chaos_test /test/rudra_test"
    echo
    echo "Press Enter to SSH, or Ctrl+C to cancel"
    read
    ssh root@"$VM_IP"
fi

echo
echo "=== Tests Complete ==="
echo



