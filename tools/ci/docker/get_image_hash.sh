#!/usr/bin/env bash
set -euo pipefail

# Extract Bazel's action hash for the OCI image build
# This is the truly hermetic cache key - same inputs = same hash

bazel aquery //tools/ci/docker:xibalba_dev_env 2>/dev/null \
  | grep -A15 "Mnemonic: OCIImage" \
  | grep "ActionKey:" \
  | awk '{print $2}' \
  | cut -c1-16  # Use first 16 chars for shorter tags

