#!/bin/bash
set -euo pipefail

# Setup pre-commit hooks for Xibalba development
# This script installs pre-commit (if needed) and configures git hooks

echo "════════════════════════════════════════════════════════════════"
echo "  Xibalba Pre-Commit Setup"
echo "════════════════════════════════════════════════════════════════"
echo

# Check if pre-commit is installed
if command -v pre-commit &> /dev/null; then
    echo "✓ pre-commit is already installed ($(pre-commit --version))"
else
    echo "Installing pre-commit..."
    
    # Try pipx first (recommended for modern Python environments)
    if command -v pipx &> /dev/null; then
        echo "  Using pipx..."
        pipx install pre-commit
    else
        echo "❌ Error: pipx not found"
        echo
        echo "Modern Python (3.11+) requires pipx for installing tools."
        echo
        echo "Install pipx first:"
        echo "  sudo apt install pipx"
        echo "  pipx ensurepath  # Add ~/.local/bin to PATH"
        echo
        echo "Then re-run this script:"
        echo "  bazel run //tools:setup_precommits"
        exit 1
    fi
    
    # Verify installation
    if ! command -v pre-commit &> /dev/null; then
        echo "❌ Error: pre-commit installed but not in PATH"
        echo
        echo "Run pipx's PATH setup:"
        echo "  pipx ensurepath"
        echo "  source ~/.bashrc  # Or restart your shell"
        echo
        echo "Then re-run this script."
        exit 1
    fi
    
    echo "✓ pre-commit installed successfully"
fi

echo

# Install the git hooks
echo "Installing git hooks..."
if pre-commit install; then
    echo "✓ Git hooks installed"
else
    echo "❌ Failed to install git hooks"
    exit 1
fi

echo

# Run against all files as a test
echo "Running pre-commit checks on all files (test run)..."
if pre-commit run --all-files; then
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

