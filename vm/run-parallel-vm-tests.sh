#!/bin/bash
set -euo pipefail

# Parallel VM Testing with Progressive Consistency Models
# 
# Creates and runs tests on multiple VMs simultaneously.
# Each VM runs the GAUNTLET: tests the same filesystem with escalating
# consistency models to quantify departures from ideal behavior.
#
# Testing Strategy per VM:
#   1. EVENTUAL (baseline) → Should PASS (no duplicates)
#   2. WEAK (POSIX std)    → Should PASS (POSIX guarantee)
#   3. STRICT (ideal)      → Quantify weak consistency (expected failures)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Detect SSH key (from create_test_vm.sh or user's home)
if [ -n "${XIBALBA_SSH_KEY:-}" ] && [ -f "$XIBALBA_SSH_KEY" ]; then
    SSH_OPTS="-i $XIBALBA_SSH_KEY -o StrictHostKeyChecking=no -o ConnectTimeout=2"
    echo "INFO: Using SSH key: $XIBALBA_SSH_KEY"
elif [ -f "/tmp/xibalba-ssh-keys/id_rsa" ]; then
    SSH_OPTS="-i /tmp/xibalba-ssh-keys/id_rsa -o StrictHostKeyChecking=no -o ConnectTimeout=2"
    echo "INFO: Using temporary SSH key: /tmp/xibalba-ssh-keys/id_rsa"
elif [ -n "${HOME:-}" ] && [ -f "$HOME/.ssh/id_rsa" ]; then
    SSH_OPTS="-i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no -o ConnectTimeout=2"
else
    SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=2"
    echo "WARNING: No SSH key found, relying on default auth"
fi

# Default configuration (per consistency model)
DURATION="${XIBALBA_DURATION:-300}"  # 5 minutes per model (15 min total per VM)
READERS="${XIBALBA_READERS:-10}"
WRITERS="${XIBALBA_WRITERS:-3}"

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
echo "  Duration: $DURATION seconds PER MODEL"
echo "  Readers: $READERS threads"
echo "  Writers: $WRITERS threads"
echo
echo "Progressive Testing (per VM):"
echo "  1. EVENTUAL → Baseline (only duplicates are bugs)"
echo "  2. WEAK     → POSIX guarantee (should pass)"
echo "  3. STRICT   → Quantify departures (measure weak consistency)"
echo
echo "Total time per VM: ~$((DURATION * 3 / 60)) minutes (3 models × ${DURATION}s)"
echo
echo "Results will be saved to:"
echo "  $RESULTS_DIR"
echo
echo "════════════════════════════════════════════════════════════════"
echo

# Prerequisites are validated by Bazel (via //vm:verify_host_deps test)
# This script assumes all required tools are available

# Get package path (passed by Bazel as first argument or from environment)
if [ $# -gt 0 ]; then
    PACKAGE_PATH="$1"
elif [ -n "${XIBALBA_PACKAGE:-}" ]; then
    PACKAGE_PATH="$XIBALBA_PACKAGE"
else
    # Fallback for direct execution
    PACKAGE_PATH="$PROJECT_ROOT/bazel-bin/packaging/xibalba_0.1.0_amd64.deb"
fi

if [ ! -f "$PACKAGE_PATH" ]; then
    echo "❌ Error: Package not found: $PACKAGE_PATH"
    echo "Build it first: bazel build //packaging:xibalba-deb"
    exit 1
fi

echo "Using package: $PACKAGE_PATH"
echo

# Function to setup and run test in a VM (runs synchronously, caller backgrounds it)
run_vm_test() {
    local vm_name=$1
    local filesystem=$2
    local log_file="$RESULTS_DIR/${vm_name}.log"
    local exit_code_file="$RESULTS_DIR/${vm_name}.exit"
    
    # Redirect to log file
    exec > "$log_file" 2>&1
    
    echo "[${vm_name}] Starting at $(date +%H:%M:%S)"
    
    # Check if VM exists, create if not
    if ! virsh list --all | grep -q "$vm_name"; then
        echo "[${vm_name}] Creating VM..."
        # Find create_test_vm.sh (in bazel runfiles or workspace)
        CREATE_VM_SCRIPT=""
        POSSIBLE_LOCATIONS=(
            "$SCRIPT_DIR/create_test_vm.sh"                    # Bazel runfiles (sibling)
            "$SCRIPT_DIR/../vm/create_test_vm.sh"              # Bazel runfiles (relative)
            "$PROJECT_ROOT/vm/create_test_vm.sh"               # Direct execution
            "$(dirname "$0")/create_test_vm.sh"                # Same dir as this script
        )
        
        for loc in "${POSSIBLE_LOCATIONS[@]}"; do
            if [ -f "$loc" ]; then
                CREATE_VM_SCRIPT="$loc"
                echo "[${vm_name}] Found create_test_vm.sh at: $loc"
                break
            fi
        done
        
        if [ -z "$CREATE_VM_SCRIPT" ]; then
            echo "[${vm_name}] ❌ Error: create_test_vm.sh not found"
            echo "[${vm_name}] Tried:"
            for loc in "${POSSIBLE_LOCATIONS[@]}"; do
                echo "[${vm_name}]   - $loc"
            done
            echo 1 > "$exit_code_file"
            return 1
        fi
        
        "$CREATE_VM_SCRIPT" --name "$vm_name" --ram 2048 --disk 10
    else
        echo "[${vm_name}] VM exists, starting..."
        virsh start "$vm_name" 2>/dev/null || true
    fi
    
    # Wait for VM to be ready
    echo "[${vm_name}] Waiting for SSH..."
    # shellcheck disable=SC2086
    timeout 90 bash -c "until ssh $SSH_OPTS root@$vm_name true 2>/dev/null; do sleep 2; done" || {
        echo "[${vm_name}] ❌ Failed to connect via SSH"
        echo 1 > "$exit_code_file"
        return 1
    }
    
    # Check if xibalba is already installed (pre-built image)
    # shellcheck disable=SC2086
    if ssh $SSH_OPTS "root@${vm_name}" "which xibalba-gauntlet >/dev/null 2>&1"; then
        echo "[${vm_name}] ✓ Xibalba already installed (using pre-built image)"
    else
        echo "[${vm_name}] Deploying Xibalba..."
        # shellcheck disable=SC2086
        scp $SSH_OPTS "$PACKAGE_PATH" "root@${vm_name}:/tmp/" || {
            echo "[${vm_name}] ❌ Failed to copy package"
            echo 1 > "$exit_code_file"
            return 1
        }
        
        # Use dpkg -i (fast) - dependencies are pre-installed via cloud-init
        # shellcheck disable=SC2086
        ssh $SSH_OPTS "root@${vm_name}" "dpkg -i /tmp/xibalba_0.1.0_amd64.deb" || {
            echo "[${vm_name}] ❌ Failed to install package"
            echo 1 > "$exit_code_file"
            return 1
        }
    fi
    
    # Run gauntlet (progressive testing: EVENTUAL → WEAK → STRICT)
    echo "[${vm_name}] Running progressive gauntlet on $filesystem..."
    echo "[${vm_name}]   1. EVENTUAL (baseline) - should PASS"
    echo "[${vm_name}]   2. WEAK (POSIX) - should PASS"
    echo "[${vm_name}]   3. STRICT (ideal) - quantify departures"
    
    # shellcheck disable=SC2086,SC2029
    if ssh $SSH_OPTS "root@${vm_name}" \
        "XIBALBA_DURATION=$DURATION XIBALBA_READERS=$READERS XIBALBA_WRITERS=$WRITERS xibalba-gauntlet $filesystem"; then
        echo "[${vm_name}] ✅ TEST PASSED at $(date +%H:%M:%S)"
        
        # Get gauntlet results (comprehensive summary + individual model results)
        # shellcheck disable=SC2086
        ssh $SSH_OPTS "root@${vm_name}" \
            "cat /var/log/xibalba/gauntlet/latest-gauntlet.json" > "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || true
        # shellcheck disable=SC2086
        ssh $SSH_OPTS "root@${vm_name}" \
            "cat /var/log/xibalba/gauntlet/latest-gauntlet.txt" > "$RESULTS_DIR/${vm_name}-gauntlet.txt" 2>/dev/null || true
        
        echo 0 > "$exit_code_file"
        return 0
    else
        echo "[${vm_name}] ❌ TEST FAILED at $(date +%H:%M:%S)"
        
        # Get gauntlet failure details
        # shellcheck disable=SC2086
        ssh $SSH_OPTS "root@${vm_name}" \
            "cat /var/log/xibalba/gauntlet/latest-gauntlet.json" > "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || true
        # shellcheck disable=SC2086
        ssh $SSH_OPTS "root@${vm_name}" \
            "cat /var/log/xibalba/gauntlet/latest-gauntlet.txt" > "$RESULTS_DIR/${vm_name}-gauntlet.txt" 2>/dev/null || true
        
        echo 1 > "$exit_code_file"
        return 1
    fi
}

# Start all VM tests in parallel
declare -a PIDS=()
declare -a VM_NAMES=()
declare -a FILESYSTEMS=()

for vm_config in "${VMS[@]}"; do
    IFS=':' read -r vm_name fs <<< "$vm_config"
    
    echo "Starting VM test: $vm_name ($fs)"
    
    # Start function in background and capture its PID
    run_vm_test "$vm_name" "$fs" &
    pid=$!
    
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
    exit_code_file="$RESULTS_DIR/${vm_name}.exit"
    
    echo "Waiting for ${vm_name} (PID: $pid)..."
    wait "$pid" 2>/dev/null || true
    
    # Read exit code from file (more reliable than wait with redirected functions)
    if [ -f "$exit_code_file" ]; then
        exit_code=$(cat "$exit_code_file")
    else
        exit_code=1  # Default to failure if no exit code file
    fi
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
  "progressive_testing": ["eventual", "weak", "strict"],
  "vms": [
EOF

first=true
for i in "${!VM_NAMES[@]}"; do
    vm_name=${VM_NAMES[$i]}
    fs=${FILESYSTEMS[$i]}
    exit_code=${EXIT_CODES[$i]}
    
    [ "$first" = false ] && echo "    ," >> "$RESULTS_DIR/summary.json"
    first=false
    
    if [ -f "$RESULTS_DIR/${vm_name}-gauntlet.json" ]; then
        # Extract metrics from gauntlet (aggregate across all models)
        # Count total operations across all 3 models
        ops=$(jq -r '[.results[].total_operations] | add // 0' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "0")
        bugs=$(jq -r '[.results[].bugs_found] | add // 0' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "0")
        missing=$(jq -r '[.results[].missing_entries] | add // 0' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "0")
        phantom=$(jq -r '[.results[].phantom_entries] | add // 0' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "0")
        duplicate=$(jq -r '[.results[].duplicate_entries] | add // 0' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "0")
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
      "gauntlet_file": "${vm_name}-gauntlet.json"
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
        
        # Show gauntlet metrics if available
        if [ -f "$RESULTS_DIR/${vm_name}-gauntlet.json" ]; then
            # Show per-model results
            eventual_status=$(jq -r '.results[] | select(.model == "eventual") | .status' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "N/A")
            weak_status=$(jq -r '.results[] | select(.model == "weak") | .status' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "N/A")
            strict_status=$(jq -r '.results[] | select(.model == "strict") | .status' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "N/A")
            strict_bugs=$(jq -r '.results[] | select(.model == "strict") | .bugs_found' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "0")
            
            echo "   Progressive Results:"
            echo "     EVENTUAL: $eventual_status (baseline)"
            echo "     WEAK:     $weak_status (POSIX)"
            echo "     STRICT:   $strict_status (departures: $strict_bugs)"
        fi
    else
        echo "❌ ${vm_name} ($fs): FAILED (exit code: $exit_code)"
        all_passed=false
        
        # Show failure details if available
        if [ -f "$RESULTS_DIR/${vm_name}-gauntlet.json" ]; then
            # Show which models failed
            eventual_bugs=$(jq -r '.results[] | select(.model == "eventual") | .bugs_found' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "0")
            weak_bugs=$(jq -r '.results[] | select(.model == "weak") | .bugs_found' "$RESULTS_DIR/${vm_name}-gauntlet.json" 2>/dev/null || echo "0")
            
            [ "$eventual_bugs" -gt 0 ] && echo "   ⚠️ EVENTUAL bugs: $eventual_bugs (CRITICAL - duplicates!)"
            [ "$weak_bugs" -gt 0 ] && echo "   ⚠️ WEAK bugs: $weak_bugs (POSIX violation!)"
        fi
    fi
    
    echo "   Log: $RESULTS_DIR/${vm_name}.log"
    echo "   Gauntlet: $RESULTS_DIR/${vm_name}-gauntlet.json"
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

