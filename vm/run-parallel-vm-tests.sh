#!/bin/bash
set -euo pipefail

# Parallel VM Testing
# Creates and runs tests on multiple VMs simultaneously
# Each VM tests a different filesystem

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default configuration
DURATION="${XIBALBA_DURATION:-300}"  # 5 minutes
READERS="${XIBALBA_READERS:-10}"
WRITERS="${XIBALBA_WRITERS:-3}"
MODEL="${XIBALBA_MODEL:-weak}"

# Results directory with timestamp
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
RESULTS_BASE="${XIBALBA_RESULTS_DIR:-$PROJECT_ROOT/test-results}"
RESULTS_DIR="$RESULTS_BASE/parallel-vm-tests-$TIMESTAMP"
mkdir -p "$RESULTS_DIR"

# VMs to create (VM_NAME:FILESYSTEM)
declare -a VMS=(
    "xibalba-ext4:ext4"
    "xibalba-zfs:zfs"
)

echo "════════════════════════════════════════════════════════════════"
echo "    Xibalba Parallel VM Testing"
echo "════════════════════════════════════════════════════════════════"
echo
echo "Configuration:"
echo "  VMs: ${#VMS[@]}"
for vm_config in "${VMS[@]}"; do
    IFS=':' read -r vm_name fs <<< "$vm_config"
    echo "    - $vm_name → $fs"
done
echo "  Duration: $DURATION seconds"
echo "  Readers: $READERS threads"
echo "  Writers: $WRITERS threads"
echo "  Model: $MODEL"
echo
echo "Results will be saved to:"
echo "  $RESULTS_DIR"
echo
echo "════════════════════════════════════════════════════════════════"
echo

# Check prerequisites
echo "Checking prerequisites..."
if ! command -v virsh &> /dev/null; then
    echo "❌ Error: libvirt not installed"
    echo "Run: bazel run //vm:check_prerequisites"
    exit 1
fi
echo "✓ Prerequisites OK"
echo

# Build Debian package
echo "Building Xibalba package..."
cd "$PROJECT_ROOT"
bazel build //packaging:xibalba-deb
PACKAGE_PATH="$PROJECT_ROOT/bazel-bin/packaging/xibalba_0.1.0_amd64.deb"
echo "✓ Package built: $PACKAGE_PATH"
echo

# Function to setup and run test in a VM
run_vm_test() {
    local vm_name=$1
    local filesystem=$2
    local log_file="$RESULTS_DIR/${vm_name}.log"
    
    {
        echo "[${vm_name}] Starting at $(date +%H:%M:%S)"
        
        # Check if VM exists, create if not
        if ! virsh list --all | grep -q "$vm_name"; then
            echo "[${vm_name}] Creating VM..."
            bazel run //vm:create_vm -- --name "$vm_name" --memory 2048 --cpus 2
        else
            echo "[${vm_name}] VM exists, starting..."
            virsh start "$vm_name" 2>/dev/null || true
        fi
        
        # Wait for VM to be ready
        echo "[${vm_name}] Waiting for SSH..."
        timeout 60 bash -c "until ssh -o StrictHostKeyChecking=no -o ConnectTimeout=2 root@$vm_name true 2>/dev/null; do sleep 2; done" || {
            echo "[${vm_name}] ❌ Failed to connect via SSH"
            return 1
        }
        
        # Deploy package
        echo "[${vm_name}] Deploying Xibalba..."
        scp -o StrictHostKeyChecking=no "$PACKAGE_PATH" "root@${vm_name}:/tmp/" || {
            echo "[${vm_name}] ❌ Failed to copy package"
            return 1
        }
        
        ssh -o StrictHostKeyChecking=no "root@${vm_name}" "apt update && apt install -y /tmp/xibalba_0.1.0_amd64.deb" || {
            echo "[${vm_name}] ❌ Failed to install package"
            return 1
        }
        
        # Run test
        echo "[${vm_name}] Running test on $filesystem..."
        if ssh -o StrictHostKeyChecking=no "root@${vm_name}" \
            "XIBALBA_FILESYSTEM=$filesystem XIBALBA_DURATION=$DURATION XIBALBA_READERS=$READERS XIBALBA_WRITERS=$WRITERS XIBALBA_MODEL=$MODEL xibalba-test-runner"; then
            echo "[${vm_name}] ✅ TEST PASSED at $(date +%H:%M:%S)"
            
            # Get results (both JSON and text)
            ssh -o StrictHostKeyChecking=no "root@${vm_name}" \
                "cat /var/log/xibalba/latest-${filesystem}.json" > "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || true
            ssh -o StrictHostKeyChecking=no "root@${vm_name}" \
                "cat /var/log/xibalba/latest-${filesystem}.txt" > "$RESULTS_DIR/${vm_name}-results.txt" 2>/dev/null || true
            
            return 0
        else
            echo "[${vm_name}] ❌ TEST FAILED at $(date +%H:%M:%S)"
            
            # Get failure details (both JSON and text)
            ssh -o StrictHostKeyChecking=no "root@${vm_name}" \
                "cat /var/log/xibalba/latest-${filesystem}.json" > "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || true
            ssh -o StrictHostKeyChecking=no "root@${vm_name}" \
                "cat /var/log/xibalba/latest-${filesystem}.txt" > "$RESULTS_DIR/${vm_name}-results.txt" 2>/dev/null || true
            
            return 1
        fi
    } > "$log_file" 2>&1 &
    
    echo $!  # Return background PID
}

# Start all VM tests in parallel
declare -a PIDS=()
declare -a VM_NAMES=()
declare -a FILESYSTEMS=()

for vm_config in "${VMS[@]}"; do
    IFS=':' read -r vm_name fs <<< "$vm_config"
    
    echo "Starting VM test: $vm_name ($fs)"
    pid=$(run_vm_test "$vm_name" "$fs")
    PIDS+=("$pid")
    VM_NAMES+=("$vm_name")
    FILESYSTEMS+=("$fs")
    
    # Stagger VM starts by 5 seconds to avoid resource contention
    sleep 5
done

echo
echo "All VM tests started!"
echo "  PIDs: ${PIDS[*]}"
echo
echo "Watching logs..."
echo "  Use 'tail -f $RESULTS_DIR/*.log' to monitor progress"
echo
echo "Waiting for tests to complete..."
echo

# Wait for all tests and collect exit codes
declare -a EXIT_CODES=()
for i in "${!PIDS[@]}"; do
    pid=${PIDS[$i]}
    vm_name=${VM_NAMES[$i]}
    
    echo "Waiting for ${vm_name} (PID: $pid)..."
    wait "$pid"
    exit_code=$?
    EXIT_CODES+=("$exit_code")
    
    # Show log tail
    echo
    echo "════════════════════════════════════════════════════════════════"
    echo "  ${vm_name} Log (last 20 lines):"
    echo "════════════════════════════════════════════════════════════════"
    tail -20 "$RESULTS_DIR/${vm_name}.log"
    echo
done

# Create comprehensive summary JSON
echo "Creating comprehensive summary..."
cat > "$RESULTS_DIR/summary.json" <<EOF
{
  "timestamp": "$(date -Iseconds)",
  "duration_seconds": $DURATION,
  "readers": $READERS,
  "writers": $WRITERS,
  "model": "$MODEL",
  "vms": [
EOF

first=true
for i in "${!VM_NAMES[@]}"; do
    vm_name=${VM_NAMES[$i]}
    fs=${FILESYSTEMS[$i]}
    exit_code=${EXIT_CODES[$i]}
    
    [ "$first" = false ] && echo "    ," >> "$RESULTS_DIR/summary.json"
    first=false
    
    if [ -f "$RESULTS_DIR/${vm_name}-results.json" ]; then
        # Extract metrics from individual result
        ops=$(jq -r '.results.total_operations // 0' "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || echo "0")
        bugs=$(jq -r '.results.bugs_found // 0' "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || echo "0")
        missing=$(jq -r '.results.missing_entries // 0' "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || echo "0")
        phantom=$(jq -r '.results.phantom_entries // 0' "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || echo "0")
        duplicate=$(jq -r '.results.duplicate_entries // 0' "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || echo "0")
    else
        ops=0
        bugs=0
        missing=0
        phantom=0
        duplicate=0
    fi
    
    cat >> "$RESULTS_DIR/summary.json" <<EOF
    {
      "vm_name": "$vm_name",
      "filesystem": "$fs",
      "status": "$([ "$exit_code" -eq 0 ] && echo "PASS" || echo "FAIL")",
      "exit_code": $exit_code,
      "total_operations": $ops,
      "bugs_found": $bugs,
      "missing_entries": $missing,
      "phantom_entries": $phantom,
      "duplicate_entries": $duplicate,
      "log_file": "${vm_name}.log",
      "results_file": "${vm_name}-results.json"
    }
EOF
done

cat >> "$RESULTS_DIR/summary.json" <<EOF

  ]
}
EOF

# Summary
echo
echo "════════════════════════════════════════════════════════════════"
echo "    Test Results Summary"
echo "════════════════════════════════════════════════════════════════"
echo

all_passed=true
for i in "${!VM_NAMES[@]}"; do
    vm_name=${VM_NAMES[$i]}
    fs=${FILESYSTEMS[$i]}
    exit_code=${EXIT_CODES[$i]}
    
    if [ "$exit_code" -eq 0 ]; then
        echo "✅ ${vm_name} ($fs): PASSED"
        
        # Show metrics if available
        if [ -f "$RESULTS_DIR/${vm_name}-results.json" ]; then
            ops=$(jq -r '.results.total_operations // "N/A"' "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || echo "N/A")
            missing=$(jq -r '.results.missing_entries // "N/A"' "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || echo "N/A")
            phantom=$(jq -r '.results.phantom_entries // "N/A"' "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || echo "N/A")
            duplicate=$(jq -r '.results.duplicate_entries // "N/A"' "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || echo "N/A")
            echo "   Operations: $ops | Missing: $missing | Phantom: $phantom | Duplicate: $duplicate"
        fi
    else
        echo "❌ ${vm_name} ($fs): FAILED (exit code: $exit_code)"
        all_passed=false
        
        # Show failure details if available
        if [ -f "$RESULTS_DIR/${vm_name}-results.json" ]; then
            bugs=$(jq -r '.results.bugs_found // "N/A"' "$RESULTS_DIR/${vm_name}-results.json" 2>/dev/null || echo "N/A")
            echo "   Bugs found: $bugs"
        fi
    fi
    
    echo "   Log: $RESULTS_DIR/${vm_name}.log"
    echo "   Results: $RESULTS_DIR/${vm_name}-results.json"
    echo
done

echo
echo "All results saved to: $RESULTS_DIR"
echo "  - summary.json: Comprehensive summary of all tests"
echo "  - *.log: Individual VM logs"
echo "  - *-results.json: Individual test results"
echo

echo "════════════════════════════════════════════════════════════════"
echo

if $all_passed; then
    echo "🎉 All VM tests PASSED!"
    echo
    echo "To analyze results:"
    echo "  jq . $RESULTS_DIR/summary.json"
    echo "  cat $RESULTS_DIR/*.log"
    exit 0
else
    echo "⚠️  Some VM tests FAILED"
    echo
    echo "To analyze failures:"
    echo "  jq . $RESULTS_DIR/summary.json"
    echo "  cat $RESULTS_DIR/*.log"
    exit 1
fi

