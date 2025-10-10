#!/bin/bash
# Run pause controller without sudo
BIN_DIR="$(cd "$(dirname "$0")/bin" && pwd)"
cd "$BIN_DIR"
./pause_controller "$@"
