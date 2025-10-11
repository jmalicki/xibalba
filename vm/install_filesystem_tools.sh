#!/bin/bash
set -euo pipefail

# Install filesystem tools for Xibalba testing
#
# This script installs all the filesystem utilities needed to test
# the filesystems documented in FILESYSTEM-CONSISTENCY-MODELS.md

echo "=== Installing Filesystem Tools for Xibalba ==="
echo

# Update package list
echo "Updating package list..."
sudo apt-get update -qq

# Install filesystem tools
echo "Installing filesystem utilities..."

# ext4 (usually already available)
sudo apt-get install -y e2fsprogs

# XFS
sudo apt-get install -y xfsprogs

# btrfs
sudo apt-get install -y btrfs-progs

# ZFS (from contrib repo)
# Note: ZFS may require enabling additional repositories
if apt-cache search zfsutils-linux | grep -q zfsutils-linux; then
    echo "  Installing ZFS tools..."
    sudo apt-get install -y zfsutils-linux
else
    echo "  ⚠️  ZFS tools not available (may need contrib repo enabled)"
fi

# F2FS
sudo apt-get install -y f2fs-tools

# NILFS2
sudo apt-get install -y nilfs-tools

# NFS (client and server)
sudo apt-get install -y nfs-common nfs-kernel-server

# exFAT
sudo apt-get install -y exfatprogs

# FUSE utilities
sudo apt-get install -y fuse3

# General filesystem utilities
sudo apt-get install -y \
    util-linux \
    dosfstools \
    ntfs-3g \
    cifs-utils

echo
echo "✅ Filesystem tools installed!"
echo
echo "Installed filesystems:"
echo "  - ext4 (e2fsprogs)"
echo "  - XFS (xfsprogs)"
echo "  - btrfs (btrfs-progs)"
echo "  - ZFS (zfsutils-linux, if available)"
echo "  - F2FS (f2fs-tools)"
echo "  - NILFS2 (nilfs-tools)"
echo "  - NFS (nfs-common, nfs-kernel-server)"
echo "  - exFAT (exfatprogs)"
echo "  - NTFS (ntfs-3g)"
echo "  - CIFS/SMB (cifs-utils)"
echo
echo "You can now create and mount these filesystems for testing."

