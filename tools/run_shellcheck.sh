#!/bin/bash
set -euo pipefail

# Run shellcheck on all shell scripts
# Bazel-managed shellcheck binary (hermetic)

# Workspace location (hardcoded for lint test - needs real sources)
WORKSPACE="/home/jmalicki/src/rudra"

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
