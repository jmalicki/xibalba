#!/bin/bash
set -euo pipefail

# Setup pre-commit hooks for Xibalba development
# This script installs pre-commit (if needed) and configures git hooks

echo "════════════════════════════════════════════════════════════════"
echo "  Xibalba Pre-Commit Setup"
echo "════════════════════════════════════════════════════════════════"
echo

# Bazel provides pre-commit via runfiles - find it
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRECOMMIT_BIN=""

# Search for pre-commit in Bazel runfiles
RUNFILES_LOCATIONS=(
    "$SCRIPT_DIR/precommit"
    "$SCRIPT_DIR/../tools/precommit" 
    "$(dirname "$SCRIPT_DIR")/tools/precommit"
)

for loc in "${RUNFILES_LOCATIONS[@]}"; do
    if [ -x "$loc" ]; then
        PRECOMMIT_BIN="$loc"
        echo "✓ Using Bazel-managed pre-commit: $loc"
        break
    fi
done

if [ -z "$PRECOMMIT_BIN" ]; then
    echo "❌ Error: Could not find pre-commit in Bazel runfiles"
    echo "This shouldn't happen - please report this bug."
    exit 1
fi

echo

# Install the git hooks
echo "Installing git hooks..."
if "$PRECOMMIT_BIN" install; then
    echo "✓ Git hooks installed"
else
    echo "❌ Failed to install git hooks"
    exit 1
fi

echo

# Run against all files as a test
echo "Running pre-commit checks on all files (test run)..."
if "$PRECOMMIT_BIN" run --all-files; then
    echo
    echo "✓ All checks passed!"
else
    echo
    echo "⚠️  Some checks failed, but hooks are installed."
    echo "   Pre-commit will now run automatically on each commit."
    echo "   Fix the issues above before your next commit."
fi

echo
echo "════════════════════════════════════════════════════════════════"
echo "  Setup Complete!"
echo "════════════════════════════════════════════════════════════════"
echo
echo "Pre-commit hooks will now run automatically before each commit."
echo
echo "Checks include:"
echo "  • shellcheck (bash script linting)"
echo "  • shfmt (bash script formatting)"
echo "  • trailing whitespace"
echo "  • YAML/JSON validation"
echo
echo "To skip hooks temporarily (not recommended):"
echo "  git commit --no-verify"
echo
echo "To manually run checks:"
echo "  pre-commit run --all-files"
echo

