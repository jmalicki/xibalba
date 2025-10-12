#!/bin/bash
# Generate cache key from lock files
# Cache key changes when Bazel deps or apt packages change

set -euo pipefail

# When run via Bazel, files are passed as arguments
# When run directly, use relative paths

if [ $# -eq 2 ]; then
    # Running from Bazel - files passed as args
    BAZEL_LOCK="$1"
    PKG_LOCK="$2"
else
    # Running directly - use relative paths
    cd "$(dirname "$0")/../../.."
    BAZEL_LOCK="MODULE.bazel.lock"
    PKG_LOCK="tools/ci/docker/packages.lock.json"
fi

# Hash MODULE.bazel.lock (Bazel dependencies)
BAZEL_HASH=$(sha256sum "$BAZEL_LOCK" | cut -d' ' -f1)

# Hash packages.lock.json (apt package versions)
PKG_HASH=$(sha256sum "$PKG_LOCK" | cut -d' ' -f1)

# Combine: first 8 chars of each
echo "${BAZEL_HASH:0:8}-${PKG_HASH:0:8}"

