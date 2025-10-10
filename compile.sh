#!/bin/bash
# Compile all RUDRA binaries
#
# Run this to build everything (no sudo needed)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"

echo "=== Compiling RUDRA Binaries ==="
echo ""
echo "Output directory: $BIN_DIR"
echo ""

# Create bin directory
mkdir -p "$BIN_DIR"

# Compile DirectoryReader + Simple Chaos Test
echo "Compiling simple_chaos_test..."
gcc -o "$BIN_DIR/simple_chaos_test" \
    "$SCRIPT_DIR/chaos/simple_chaos_test.c" \
    "$SCRIPT_DIR/common/dir_reader.c" \
    -I"$SCRIPT_DIR" -pthread
echo "  ✓ $BIN_DIR/simple_chaos_test"

# Compile Pause Controller
echo ""
echo "Compiling pause_controller..."
gcc -o "$BIN_DIR/pause_controller" \
    "$SCRIPT_DIR/chaos/pause_controller.c" \
    -lbpf -lelf
echo "  ✓ $BIN_DIR/pause_controller"

# Compile eBPF program
echo ""
echo "Compiling pause_injector.bpf.o..."
clang -g -O2 -target bpf \
    -D__TARGET_ARCH_x86_64 \
    -I/usr/include/x86_64-linux-gnu \
    -c "$SCRIPT_DIR/chaos/pause_injector.bpf.c" \
    -o "$BIN_DIR/pause_injector.bpf.o"
echo "  ✓ $BIN_DIR/pause_injector.bpf.o"

echo ""
echo "=== Build Complete ==="
echo ""
echo "Binaries in: $BIN_DIR"
ls -lh "$BIN_DIR"
echo ""
echo "Next step: Grant capabilities"
echo "  sudo ./grant_caps.sh"

