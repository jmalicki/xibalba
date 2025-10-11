#!/bin/bash
set -euo pipefail

# Run shellcheck on all shell scripts
# Bazel-managed shellcheck binary (hermetic)

# Find workspace via git (works locally and in CI)
if command -v git &> /dev/null && git rev-parse --show-toplevel &> /dev/null 2>&1; then
    WORKSPACE=$(git rev-parse --show-toplevel)
else
    # Fallback: navigate from script location
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    WORKSPACE="$(cd "$SCRIPT_DIR/.." && pwd)"
fi

# Find Bazel-provided shellcheck
RUNFILES_DIR="${RUNFILES_DIR:-$(dirname "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)")}"
SHELLCHECK="$RUNFILES_DIR/_main/external/shellcheck/shellcheck-v0.9.0/shellcheck"

if [ ! -x "$SHELLCHECK" ]; then
    # Fallback to system shellcheck
    if command -v shellcheck &> /dev/null; then
        SHELLCHECK="shellcheck"
    else
        echo "❌ shellcheck not found"
        exit 1
    fi
fi

echo "Checking all vm/*.sh scripts with shellcheck..."
echo "Using: $SHELLCHECK"
echo

# Run shellcheck on all scripts at once
if "$SHELLCHECK" "$WORKSPACE"/vm/*.sh; then
    echo
    echo "✅ All shell scripts passed shellcheck!"
    exit 0
else
    echo
    echo "❌ Shell scripts have lint errors (see above)"
    exit 1
fi
