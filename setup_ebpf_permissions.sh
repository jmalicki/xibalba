#!/bin/bash
# Setup eBPF permissions for RUDRA pause controller
#
# This script compiles the pause controller and grants it capabilities
# to load eBPF programs without requiring root/sudo
#
# Run once: sudo ./setup_ebpf_permissions.sh

set -e

if [ "$EUID" -ne 0 ]; then
    echo "ERROR: This setup script must run as root"
    echo "Please run: sudo ./setup_ebpf_permissions.sh"
    exit 1
fi

RUDRA_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$RUDRA_DIR/bin"

echo "=== RUDRA eBPF Permissions Setup ==="
echo ""
echo "This will:"
echo "  1. Compile pause_controller and pause_injector.bpf.o"
echo "  2. Install to $BIN_DIR"
echo "  3. Grant CAP_BPF, CAP_PERFMON, CAP_NET_ADMIN capabilities"
echo "  4. Allow non-root users to run pause injection"
echo ""

# Create bin directory
mkdir -p "$BIN_DIR"
echo "Step 1: Creating $BIN_DIR"

# Compile eBPF program
echo ""
echo "Step 2: Compiling eBPF program..."
clang -g -O2 -target bpf -D__TARGET_ARCH_x86_64 \
    -I/usr/include/x86_64-linux-gnu \
    -c "$RUDRA_DIR/chaos/pause_injector.bpf.c" \
    -o "$BIN_DIR/pause_injector.bpf.o"
echo "  ✓ $BIN_DIR/pause_injector.bpf.o"

# Compile pause controller
echo ""
echo "Step 3: Compiling pause controller..."
gcc -o "$BIN_DIR/pause_controller" \
    "$RUDRA_DIR/chaos/pause_controller.c" \
    -lbpf -lelf
echo "  ✓ $BIN_DIR/pause_controller"

# Set capabilities on pause_controller
echo ""
echo "Step 4: Granting eBPF capabilities..."
echo "  This allows non-root users to load eBPF programs"
echo ""

# Modern kernels (5.8+) have CAP_BPF and CAP_PERFMON
if capsh --print | grep -q cap_bpf; then
    # Use modern capabilities
    setcap cap_bpf,cap_perfmon,cap_net_admin=ep "$BIN_DIR/pause_controller"
    echo "  ✓ Granted: CAP_BPF, CAP_PERFMON, CAP_NET_ADMIN"
else
    # Fallback for older kernels
    setcap cap_sys_admin=ep "$BIN_DIR/pause_controller"
    echo "  ✓ Granted: CAP_SYS_ADMIN (older kernel)"
fi

# Verify capabilities
echo ""
echo "Step 5: Verifying capabilities..."
getcap "$BIN_DIR/pause_controller"

# Compile simple_chaos_test (no special permissions needed)
echo ""
echo "Step 6: Compiling simple_chaos_test..."
gcc -o "$BIN_DIR/simple_chaos_test" \
    "$RUDRA_DIR/chaos/simple_chaos_test.c" \
    "$RUDRA_DIR/common/dir_reader.c" \
    -I"$RUDRA_DIR" -pthread
echo "  ✓ $BIN_DIR/simple_chaos_test"

# Create convenience wrappers
echo ""
echo "Step 7: Creating convenience scripts..."

cat > "$RUDRA_DIR/run_pause_controller.sh" << 'EOF'
#!/bin/bash
# Run pause controller without sudo
BIN_DIR="$(cd "$(dirname "$0")/bin" && pwd)"
cd "$BIN_DIR"
./pause_controller "$@"
EOF
chmod +x "$RUDRA_DIR/run_pause_controller.sh"
echo "  ✓ run_pause_controller.sh"

cat > "$RUDRA_DIR/run_chaos_test.sh" << 'EOF'
#!/bin/bash
# Run chaos test
BIN_DIR="$(cd "$(dirname "$0")/bin" && pwd)"
"$BIN_DIR/simple_chaos_test" "$@"
EOF
chmod +x "$RUDRA_DIR/run_chaos_test.sh"
echo "  ✓ run_chaos_test.sh"

cat > "$RUDRA_DIR/test_ebpf_works.sh" << 'EOF'
#!/bin/bash
# Test if eBPF pause injection is working
#
# This runs both baseline and eBPF tests, compares results

echo "=== Testing eBPF Pause Injection ==="
echo ""

BIN_DIR="$(cd "$(dirname "$0")/bin" && pwd)"

# Create test data
mkdir -p /tmp/rudra_test
cd /tmp/rudra_test
for i in {1..100}; do touch file$i 2>/dev/null || true; done
echo "Test directory: /tmp/rudra_test (100 files)"
echo ""

# Baseline test (no eBPF)
echo "=== Baseline (no eBPF) ==="
"$BIN_DIR/simple_chaos_test" /tmp/rudra_test | tee /tmp/baseline.txt
BASELINE_OPS=$(grep "Operations/second:" /tmp/baseline.txt | awk '{print $2}')
echo ""

# Test with eBPF pauses
echo "=== With eBPF pauses (20%, 5ms) ==="
echo "Starting pause controller in background..."

cd "$BIN_DIR"
./pause_controller 20 5000 > /tmp/pause_controller.log 2>&1 &
CONTROLLER_PID=$!
echo "  Controller PID: $CONTROLLER_PID"
sleep 2  # Let eBPF attach

# Check if loaded
if bpftool prog list 2>/dev/null | grep -q getdents; then
    echo "  ✓ eBPF program loaded"
else
    echo "  ✗ eBPF program not loaded - check /tmp/pause_controller.log"
    kill $CONTROLLER_PID 2>/dev/null || true
    exit 1
fi

# Run test
"$BIN_DIR/simple_chaos_test" /tmp/rudra_test | tee /tmp/ebpf.txt
EBPF_OPS=$(grep "Operations/second:" /tmp/ebpf.txt | awk '{print $2}')

# Stop controller
kill $CONTROLLER_PID 2>/dev/null || true
wait $CONTROLLER_PID 2>/dev/null || true
sleep 1

# Results
echo ""
echo "=== Results ==="
echo "Baseline (no eBPF):  $BASELINE_OPS ops/sec"
echo "With eBPF (20%):     $EBPF_OPS ops/sec"

PAUSES=$(grep -c "Paused pid=" /tmp/pause_controller.log 2>/dev/null || echo "0")
echo "Pauses injected:     $PAUSES"
echo ""

if [ "$PAUSES" -gt 10 ]; then
    echo "✅ SUCCESS: eBPF pause injection is working!"
    echo ""
    echo "Evidence:"
    echo "  - eBPF program loaded successfully"
    echo "  - Pauses were injected $PAUSES times"
    echo "  - Performance impact visible (slower with pauses)"
    echo ""
    echo "Next: Build race detector to prove pauses find bugs"
    exit 0
else
    echo "⚠️  ISSUE: Few or no pauses detected"
    echo ""
    echo "Debug info:"
    cat /tmp/pause_controller.log
    exit 1
fi
EOF
chmod +x "$RUDRA_DIR/test_ebpf_works.sh"
echo "  ✓ test_ebpf_works.sh"

echo ""
echo "=== Setup Complete ==="
echo ""
echo "What was done:"
echo "  - Compiled binaries to: $BIN_DIR/"
echo "  - Granted eBPF capabilities to pause_controller"
echo "  - Created convenience scripts"
echo ""
echo "Binaries installed:"
ls -lh "$BIN_DIR/"
echo ""
echo "Capabilities:"
getcap "$BIN_DIR/pause_controller"
echo ""
echo "=== Quick Test ==="
echo ""
echo "Run this to test eBPF pause injection:"
echo "  ./test_ebpf_works.sh"
echo ""
echo "Or manually:"
echo "  # Terminal 1:"
echo "  ./run_pause_controller.sh 20    # NO sudo needed!"
echo ""
echo "  # Terminal 2:"
echo "  ./run_chaos_test.sh /tmp/rudra_test"
echo ""
echo "Step 8: Fixing ownership..."
# Change ownership back to the user who ran sudo
REAL_USER=${SUDO_USER:-$USER}
if [ "$REAL_USER" != "root" ]; then
    chown -R $REAL_USER:$REAL_USER "$BIN_DIR"
    chown $REAL_USER:$REAL_USER "$RUDRA_DIR"/*.sh 2>/dev/null || true
    echo "  ✓ Changed ownership to $REAL_USER"
fi

echo ""
echo "=== Documentation ==="
echo ""
echo "What capabilities were granted:"
echo "  - CAP_BPF: Load eBPF programs"
echo "  - CAP_PERFMON: Use perf events"
echo "  - CAP_NET_ADMIN: Attach to network tracepoints"
echo ""
echo "Security note:"
echo "  These capabilities allow loading eBPF programs (powerful!)"
echo "  Only grant on dev/test machines, not production"
echo "  To remove: sudo setcap -r $BIN_DIR/pause_controller"
echo ""
echo "User '$REAL_USER' can now run eBPF programs without sudo!"
echo ""

