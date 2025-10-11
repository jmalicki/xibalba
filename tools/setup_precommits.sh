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
    
    # Try pipx first (recommended), then pip
    if command -v pipx &> /dev/null; then
        echo "  Using pipx..."
        pipx install pre-commit
    elif command -v pip3 &> /dev/null; then
        echo "  Using pip3..."
        pip3 install --user pre-commit
    elif command -v pip &> /dev/null; then
        echo "  Using pip..."
        pip install --user pre-commit
    else
        echo "❌ Error: Neither pipx nor pip found"
        echo
        echo "Please install pipx or pip first:"
        echo "  sudo apt install pipx  # Recommended"
        echo "  # or"
        echo "  sudo apt install python3-pip"
        exit 1
    fi
    
    # Verify installation
    if ! command -v pre-commit &> /dev/null; then
        echo "❌ Error: pre-commit installed but not in PATH"
        echo
        echo "Try adding to PATH:"
        echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
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

