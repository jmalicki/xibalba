# Xibalba CI Dev Environment

## Overview

Hermetic dev environment for CI using `rules_distroless` + `rules_oci`.

All CI dependencies (compilers, libraries, filesystem tools) are pre-installed in a container image cached in GitHub Container Registry (GHCR).

## Benefits

- ⚡ **2-3 minutes faster CI** - Pull cached image instead of `apt-get install`
- 🔒 **Hermetic** - Package versions pinned in `packages.lock.json`
- 🔄 **Cross-branch caching** - Same dependencies = same image across all PRs
- 📦 **Bazel-native** - Built with rules_distroless, no Docker daemon needed
- ✅ **Reproducible** - Lock file checked into git

## Structure

```
tools/ci/docker/
├── BUILD.bazel            # Bazel targets for OCI image
├── Dockerfile             # (For reference, not used)
├── packages.yaml          # Package manifest (what to install)
├── packages.lock.json     # Version lock file (exact versions)
├── generate_cache_key.sh  # Compute cache key from lock files
└── README.md              # This file
```

## Usage

### Update Packages

When you need to add/update/remove dependencies:

```bash
# 1. Edit packages.yaml
vim tools/ci/docker/packages.yaml

# 2. Regenerate lock file (downloads package info, resolves deps)
bazel run @ubuntu_packages//:lock

# 3. Commit the lock file
git add tools/ci/docker/packages.lock.json
git commit -m "deps: Update CI container packages"

# 4. Push - CI will rebuild image automatically
git push
```

### Build Locally

```bash
# Build OCI image
bazel build //tools/ci/docker:xibalba_dev_env

# Build and load into Docker
bazel build //tools/ci/docker:xibalba_dev_env_tarball
docker load < bazel-bin/tools/ci/docker/xibalba_dev_env_tarball/tarball.tar

# Run container
docker run -it -v $(pwd):/workspace -w /workspace xibalba-dev:local

# Test Bazel inside container
docker run --rm -v $(pwd):/workspace -w /workspace xibalba-dev:local \
  bazel test //common:state_tracker_test
```

### Check Cache Key

```bash
# Generate cache key
bazel run //tools/ci/docker:generate_cache_key

# Example output: d5ae29a4-d2e6b8d3
```

The cache key changes when:
- `MODULE.bazel.lock` changes (Bazel dependency update)
- `packages.lock.json` changes (apt package update)

## Cache Strategy

### GitHub Container Registry

Images are pushed to: `ghcr.io/jmalicki/xibalba-dev-env`

**Tags:**
- `latest` - Most recent build
- `<cache-key>` - Specific version (e.g., `d5ae29a4-d2e6b8d3`)

### Cache Hit Rate

Expected: **95%+**

Images only rebuild when dependencies change (rare).

### Cross-Branch Caching

All PRs can use the same cached image if they have the same dependencies.

## Installed Packages

### Build Dependencies
- clang-18
- libbpf-dev
- libelf-dev  
- linux-headers-generic

### Filesystem Tools
- e2fsprogs (ext4)
- xfsprogs (XFS)
- btrfs-progs (btrfs)
- zfsutils-linux (ZFS)
- f2fs-tools (F2FS)
- nilfs-tools (NILFS2)
- nfs-common (NFS)
- qemu-system-x86 (VM testing)

### Utilities
- jq
- curl
- ca-certificates

## Implementation

Built with:
- **rules_distroless** - Google's hermetic apt package manager
- **rules_oci** - OCI image builder
- **packages.lock.json** - Pins exact package versions
- **GitHub Actions** - Builds and caches images

## Troubleshooting

### Rebuild Image

```bash
# Force rebuild
bazel clean
bazel build //tools/ci/docker:xibalba_dev_env
```

### Update Lock File

```bash
# After editing packages.yaml
bazel run @ubuntu_packages//:lock
```

### Check What's in Image

```bash
docker run --rm xibalba-dev:local dpkg -l
```

## Related Documentation

- Implementation plan: `/docs/plans/RULES-DISTROLESS-IMPLEMENTATION.md`
- Research: `/docs/plans/APT-PACKAGE-RULES-RESEARCH.md`

