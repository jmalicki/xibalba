# Xibalba CI Development Environment
# 
# Contains all dependencies needed for building and testing Xibalba.
# Used in GitHub Actions to avoid repeated apt-get install.

FROM ubuntu:24.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    clang-20 \
    libbpf-dev \
    libelf-dev \
    linux-headers-generic \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/*

# Install filesystem tools
RUN apt-get update && apt-get install -y \
    e2fsprogs \
    xfsprogs \
    btrfs-progs \
    zfsutils-linux \
    f2fs-tools \
    nilfs-tools \
    nfs-common \
    qemu-system-x86-64 \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/*

# Install utilities
RUN apt-get update && apt-get install -y \
    jq \
    curl \
    ca-certificates \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /workspace

# Default command
CMD ["/bin/bash"]

