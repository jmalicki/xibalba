#!/bin/bash
# Run chaos test
BIN_DIR="$(cd "$(dirname "$0")/bin" && pwd)"
"$BIN_DIR/simple_chaos_test" "$@"
