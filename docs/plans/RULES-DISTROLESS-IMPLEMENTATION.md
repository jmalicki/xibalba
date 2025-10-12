# Docker Cache Implementation: rules_distroless Approach

## Executive Summary

**Goal:** Cache CI dependencies using `rules_distroless` + `rules_oci` + GitHub Container Registry

**Public Rules Available:** ✅ YES - `rules_distroless` by Google Container Tools

**Benefits:**
- ✅ Hermetic (package versions in lock file)
- ✅ Bazel-native (no Docker daemon)
- ✅ Reproducible (lock file in git)
- ✅ Integrates with rules_oci
- ✅ Production-ready (Google-maintained)

**Estimated Time:** 1-2 days (vs. 2 hours for Dockerfile, but worth it for hermeticity)

---

## What is rules_distroless?

**Official Project:** https://github.com/GoogleContainerTools/rules_distroless

**Purpose:** Hermetic Debian/Ubuntu package management for Bazel

**Key Features:**
- Downloads `.deb` packages at build time
- Creates OCI-compatible layers
- Pins versions in lock file (like package-lock.json)
- Works with bzlmod (modern Bazel)
- Integrates with rules_oci

**Used By:** Google's distroless images (production container images)

---

## Implementation Checklist

### 📦 Phase 0: Add Dependency (15 minutes)

- [ ] **Update MODULE.bazel**
  ```bash
  cd /home/jmalicki/src/xibalba
  ```
  
- [ ] Add to `MODULE.bazel` after `rules_oci`:
  ```python
  # Hermetic Debian package management
  bazel_dep(name = "rules_distroless", version = "0.3.7")
  ```

- [ ] **Update Bazel lockfile**
  - [ ] Run: `bazel mod deps` (check dependency graph)
  - [ ] Run: `bazel mod update` (update MODULE.bazel.lock)
  - [ ] Verify no conflicts

- [ ] **Test dependency**
  - [ ] Run: `bazel query @rules_distroless//...`
  - [ ] Should see available rules
  - [ ] No errors

---

### 🏗️ Phase 1: Create Package Manifest (30 minutes)

- [ ] **Create directory**
  ```bash
  mkdir -p tools/ci/docker
  ```

- [ ] **Create package manifest**
  - [ ] Create `tools/ci/docker/packages.yaml`:
    ```yaml
    version: 1
    
    sources:
      - channel: ubuntu noble amd64
        url: http://archive.ubuntu.com/ubuntu
    
    archs:
      - amd64
    
    packages:
      # Build dependencies (what CI needs)
      - clang-20
      - libbpf-dev
      - libelf-dev
      - linux-headers-generic
      
      # Filesystem tools (for VM tests)
      - e2fsprogs
      - xfsprogs
      - btrfs-progs
      - zfsutils-linux
      - f2fs-tools
      - nilfs-tools
      - nfs-common
      - qemu-system-x86-64
      
      # Utilities
      - jq
      - curl
      - ca-certificates
    ```

- [ ] **Register apt extension in MODULE.bazel**
  - [ ] Add after `bazel_dep` entries:
    ```python
    # Configure apt package sources
    apt = use_extension("@rules_distroless//apt:extensions.bzl", "apt")
    apt.install(
        name = "ubuntu_packages",
        lock = "//tools/ci/docker:packages.lock.json",
        manifest = "//tools/ci/docker:packages.yaml",
    )
    use_repo(apt, "ubuntu_packages")
    ```

- [ ] **Create BUILD.bazel with lock target**
  - [ ] Create `tools/ci/docker/BUILD.bazel`:
    ```python
    # Alias to update lock file
    alias(
        name = "lock",
        actual = "@ubuntu_packages//:lock",
    )
    ```

- [ ] **Generate lock file (pins exact versions)**
  - [ ] Run: `bazel run //tools/ci/docker:lock`
  - [ ] This downloads package info and creates `packages.lock.json`
  - [ ] Takes 30-60 seconds
  - [ ] Commit `packages.lock.json` to git
  - [ ] This lock file makes builds reproducible!

---

### 🐳 Phase 2: Create OCI Images with apt Packages (45 minutes)

- [ ] **Update tools/ci/docker/BUILD.bazel**
  - [ ] Add imports at top:
    ```python
    load("@rules_distroless//apt:defs.bzl", "dpkg_layer")
    load("@rules_oci//oci:defs.bzl", "oci_image", "oci_push", "oci_tarball")
    ```

- [ ] **Create build tools layer**
  - [ ] Add to BUILD.bazel:
    ```python
    # Layer 1: Build dependencies
    dpkg_layer(
        name = "build_tools_layer",
        dpkgs = [
            "@ubuntu_packages//clang-20",
            "@ubuntu_packages//libbpf-dev",
            "@ubuntu_packages//libelf-dev",
            "@ubuntu_packages//linux-headers-generic",
        ],
    )
    ```

- [ ] **Create filesystem tools layer**
  - [ ] Add to BUILD.bazel:
    ```python
    # Layer 2: Filesystem tools (separate for caching)
    dpkg_layer(
        name = "filesystem_tools_layer",
        dpkgs = [
            "@ubuntu_packages//e2fsprogs",
            "@ubuntu_packages//xfsprogs",
            "@ubuntu_packages//btrfs-progs",
            "@ubuntu_packages//zfsutils-linux",
            "@ubuntu_packages//f2fs-tools",
            "@ubuntu_packages//nilfs-tools",
            "@ubuntu_packages//nfs-common",
            "@ubuntu_packages//qemu-system-x86-64",
        ],
    )
    ```

- [ ] **Create utilities layer**
  - [ ] Add to BUILD.bazel:
    ```python
    # Layer 3: Utilities
    dpkg_layer(
        name = "utils_layer",
        dpkgs = [
            "@ubuntu_packages//jq",
            "@ubuntu_packages//curl",
            "@ubuntu_packages//ca-certificates",
        ],
    )
    ```

- [ ] **Create stacked OCI images**
  - [ ] Add to BUILD.bazel:
    ```python
    # Minimal build image (just build tools)
    oci_image(
        name = "xibalba_build_env",
        base = "@ubuntu_base",
        tars = [
            ":build_tools_layer",
            ":utils_layer",
        ],
    )
    
    # Full dev image (build + filesystem tools)
    oci_image(
        name = "xibalba_dev_env",
        base = ":xibalba_build_env",
        tars = [":filesystem_tools_layer"],
        entrypoint = ["/bin/bash"],
    )
    ```

- [ ] **Add tarball for local testing**
  - [ ] Add to BUILD.bazel:
    ```python
    # Tarball for local testing (docker load)
    oci_tarball(
        name = "xibalba_dev_env_tarball",
        image = ":xibalba_dev_env",
        repo_tags = ["xibalba-dev:local"],
    )
    ```

- [ ] **Test local build**
  - [ ] Run: `bazel build //tools/ci/docker:xibalba_dev_env`
  - [ ] Should download packages and build layers
  - [ ] Takes 2-5 minutes first time
  - [ ] Subsequent builds use Bazel cache (~5s)

---

### 🔑 Phase 3: Cache Key Generation (30 minutes)

- [ ] **Create cache key script**
  - [ ] Create `tools/ci/docker/generate_cache_key.sh`:
    ```bash
    #!/bin/bash
    # Generate cache key from lock files
    set -euo pipefail
    
    cd "$(dirname "$0")/../../.."  # Go to repo root
    
    # Hash MODULE.bazel.lock (Bazel dependencies)
    BAZEL_HASH=$(sha256sum MODULE.bazel.lock | cut -d' ' -f1)
    
    # Hash packages.lock.json (apt package versions)
    PKG_HASH=$(sha256sum tools/ci/docker/packages.lock.json | cut -d' ' -f1)
    
    # Combine: first 8 chars of each
    echo "${BAZEL_HASH:0:8}-${PKG_HASH:0:8}"
    ```
  
  - [ ] Make executable: `chmod +x tools/ci/docker/generate_cache_key.sh`

- [ ] **Add Bazel target**
  - [ ] Add to `tools/ci/docker/BUILD.bazel`:
    ```python
    sh_binary(
        name = "generate_cache_key",
        srcs = ["generate_cache_key.sh"],
        data = [
            "//:MODULE.bazel.lock",
            ":packages.lock.json",
        ],
    )
    ```

- [ ] **Test cache key**
  - [ ] Run: `bazel run //tools/ci/docker:generate_cache_key`
  - [ ] Example output: `a1b2c3d4-e5f6g7h8`
  - [ ] Run again, verify same output (reproducible)
  - [ ] Touch `packages.yaml`, run `bazel run :lock`, verify key changes

---

### 🚀 Phase 4: Push to GHCR (45 minutes)

- [ ] **Add push target**
  - [ ] Add to `tools/ci/docker/BUILD.bazel`:
    ```python
    # Push to GitHub Container Registry
    oci_push(
        name = "push_dev_env",
        image = ":xibalba_dev_env",
        repository = "ghcr.io/jmalicki/xibalba-dev-env",
        remote_tags = ["latest"],
    )
    ```

- [ ] **Create multi-tag push script**
  - [ ] Create `tools/ci/docker/push.sh`:
    ```bash
    #!/bin/bash
    # Push image with cache key + latest tags
    set -euo pipefail
    
    CACHE_KEY=$(bazel run //tools/ci/docker:generate_cache_key)
    
    echo "📦 Building image with cache key: ${CACHE_KEY}"
    
    # Build image
    bazel build //tools/ci/docker:xibalba_dev_env
    
    # Push with cache key tag
    echo "🚀 Pushing ghcr.io/jmalicki/xibalba-dev-env:${CACHE_KEY}"
    bazel run //tools/ci/docker:push_dev_env -- --tag "${CACHE_KEY}"
    
    # Push with latest tag  
    echo "🚀 Pushing ghcr.io/jmalicki/xibalba-dev-env:latest"
    bazel run //tools/ci/docker:push_dev_env -- --tag latest
    
    echo "✅ Pushed successfully"
    echo "   Cache key: ${CACHE_KEY}"
    echo "   Latest: ghcr.io/jmalicki/xibalba-dev-env:latest"
    ```
  
  - [ ] Make executable: `chmod +x tools/ci/docker/push.sh`

- [ ] **Add convenience target**
  - [ ] Add to BUILD.bazel:
    ```python
    sh_binary(
        name = "push",
        srcs = ["push.sh"],
        data = [
            ":generate_cache_key",
            ":push_dev_env",
        ],
    )
    ```

- [ ] **Test push locally (optional)**
  - [ ] Login to GHCR: `echo $GH_TOKEN | docker login ghcr.io -u jmalicki --password-stdin`
  - [ ] Run: `bazel run //tools/ci/docker:push`
  - [ ] Verify image appears at: https://github.com/jmalicki/xibalba/pkgs/container/xibalba-dev-env

---

### ⚙️ Phase 5: GitHub Actions - Build Image Workflow (1 hour)

- [ ] **Create build workflow**
  - [ ] Create `.github/workflows/build-dev-image.yml`:
    ```yaml
    name: Build Dev Container Image
    
    on:
      push:
        paths:
          - 'MODULE.bazel'
          - 'MODULE.bazel.lock'
          - 'tools/ci/docker/**'
        branches:
          - main
      workflow_dispatch:  # Manual trigger
    
    permissions:
      contents: read
      packages: write  # Push to GHCR
    
    jobs:
      build-and-push:
        runs-on: ubuntu-24.04
        
        steps:
        - uses: actions/checkout@v4
        
        - name: Setup Bazelisk
          uses: bazelbuild/setup-bazelisk@v3
        
        - name: Generate cache key
          id: cache-key
          run: |
            CACHE_KEY=$(bazel run //tools/ci/docker:generate_cache_key)
            echo "key=${CACHE_KEY}" >> $GITHUB_OUTPUT
            echo "📦 Cache key: ${CACHE_KEY}"
        
        - name: Login to GHCR
          uses: docker/login-action@v3
          with:
            registry: ghcr.io
            username: ${{ github.actor }}
            password: ${{ secrets.GITHUB_TOKEN }}
        
        - name: Check if image exists
          id: check-image
          run: |
            if docker manifest inspect ghcr.io/jmalicki/xibalba-dev-env:${{ steps.cache-key.outputs.key }} > /dev/null 2>&1; then
              echo "exists=true" >> $GITHUB_OUTPUT
              echo "✅ Image already exists with this cache key"
            else
              echo "exists=false" >> $GITHUB_OUTPUT
              echo "🔨 Image needs to be built"
            fi
        
        - name: Build and push image
          if: steps.check-image.outputs.exists == 'false'
          run: |
            echo "🔨 Building xibalba dev environment..."
            
            # Build OCI image with Bazel
            bazel build //tools/ci/docker:xibalba_dev_env
            
            # Push with cache key tag
            bazel run //tools/ci/docker:push_dev_env -- --tag "${{ steps.cache-key.outputs.key }}"
            
            # Push with latest tag
            bazel run //tools/ci/docker:push_dev_env -- --tag latest
            
            echo "✅ Image pushed successfully"
        
        - name: Skip build (cached)
          if: steps.check-image.outputs.exists == 'true'
          run: |
            echo "✅ Using cached image: ghcr.io/jmalicki/xibalba-dev-env:${{ steps.cache-key.outputs.key }}"
            echo "No rebuild needed!"
    ```

- [ ] **Test workflow**
  - [ ] Push to feature branch
  - [ ] Verify workflow triggers
  - [ ] Verify image builds
  - [ ] Check GHCR for image

---

### 🔄 Phase 6: Update CI to Use Container (1 hour)

- [ ] **Update .github/workflows/ci.yml - Unit Tests**
  - [ ] Find `unit-tests` job
  - [ ] Replace apt-get install with container:
    ```yaml
    unit-tests:
      name: Unit Tests
      runs-on: ubuntu-24.04
      container:
        image: ghcr.io/jmalicki/xibalba-dev-env:latest
        credentials:
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}
      
      steps:
      - uses: actions/checkout@v4
      
      # ✅ Dependencies already in container - NO apt-get needed!
      
      - name: Setup Bazelisk
        uses: bazelbuild/setup-bazelisk@v3
      
      - name: Run state_tracker unit tests
        run: |
          bazel test //common:state_tracker_test \
            --test_output=errors \
            --test_summary=detailed
      
      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: unit-test-results
          path: bazel-testlogs/
    ```

- [ ] **Update .github/workflows/ci.yml - Build Tests**
  - [ ] Find `build-all` job
  - [ ] Add container configuration:
    ```yaml
    build-all:
      name: Build All Targets
      runs-on: ubuntu-24.04
      container:
        image: ghcr.io/jmalicki/xibalba-dev-env:latest
        credentials:
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}
      
      steps:
      - uses: actions/checkout@v4
      
      # No apt-get install!
      
      - name: Setup Bazelisk
        uses: bazelbuild/setup-bazelisk@v3
      
      # ... rest of build steps
    ```

- [ ] **Keep fallback for robustness**
  - [ ] Add to each job AFTER container fails:
    ```yaml
      # Fallback if container unavailable
      - name: Install dependencies (fallback)
        if: failure()
        run: |
          sudo apt-get update
          sudo apt-get install -y clang-20 libbpf-dev libelf-dev linux-headers-generic
    ```

---

### 🧪 Phase 7: Testing (1-2 hours)

- [ ] **Local testing**
  - [ ] Build tarball: `bazel build //tools/ci/docker:xibalba_dev_env_tarball`
  - [ ] Load into Docker: `docker load < bazel-bin/tools/ci/docker/xibalba_dev_env_tarball/tarball.tar`
  - [ ] Run container: `docker run -it -v $(pwd):/workspace -w /workspace xibalba-dev:local`
  - [ ] Test Bazel works: `bazel test //common:state_tracker_test`
  - [ ] Verify all tools available:
    ```bash
    which clang
    which mkfs.ext4
    which mkfs.xfs
    which mkfs.btrfs
    dpkg -l | grep libbpf
    ```

- [ ] **Test cache key changes**
  - [ ] Generate baseline: `BEFORE=$(bazel run //tools/ci/docker:generate_cache_key)`
  - [ ] Edit `packages.yaml` (add a package)
  - [ ] Regenerate lock: `bazel run //tools/ci/docker:lock`
  - [ ] Generate new key: `AFTER=$(bazel run //tools/ci/docker:generate_cache_key)`
  - [ ] Verify: `[ "$BEFORE" != "$AFTER" ]`
  - [ ] Revert changes
  - [ ] Regenerate lock
  - [ ] Verify key returns to `$BEFORE`

- [ ] **Test in CI (create test PR)**
  - [ ] Create small PR that doesn't touch dependencies
  - [ ] Verify image build workflow runs
  - [ ] Verify CI uses container
  - [ ] Verify no apt-get install in logs
  - [ ] Measure time savings

- [ ] **Test cross-branch caching**
  - [ ] Create second PR from different branch
  - [ ] Verify it uses same cached image
  - [ ] Verify no rebuild happens
  - [ ] Check logs for "Using cached image"

---

### 🔑 Phase 8: Cache Key in GitHub Actions (30 minutes)

- [ ] **Optionally use cache key for image tag** 
  - [ ] Update ci.yml to compute cache key:
    ```yaml
    unit-tests:
      runs-on: ubuntu-24.04
      
      steps:
      - uses: actions/checkout@v4
      
      - name: Compute cache key
        id: cache-key
        run: |
          # Simple hash of lock files
          HASH=$(cat MODULE.bazel.lock tools/ci/docker/packages.lock.json | sha256sum | cut -d' ' -f1)
          echo "key=${HASH:0:16}" >> $GITHUB_OUTPUT
      
      - name: Login to GHCR
        uses: docker/login-action@v3
        with:
          registry: ghcr.io
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}
      
      - name: Pull dev environment
        run: |
          # Try cache key first, fall back to latest
          docker pull ghcr.io/jmalicki/xibalba-dev-env:${{ steps.cache-key.outputs.key }} || \
          docker pull ghcr.io/jmalicki/xibalba-dev-env:latest
      
      # Continue in container...
    ```

---

### 📊 Phase 9: Measure & Validate (30 minutes)

- [ ] **Measure time improvements**
  - [ ] Before (with apt-get):
    - [ ] Record total CI time
    - [ ] Record dependency install time
  - [ ] After (with container):
    - [ ] Record total CI time
    - [ ] Record container pull time
  - [ ] Calculate savings:
    - [ ] Expected: 40-60s → 5-10s per job
    - [ ] Total: ~2-3 minutes saved per CI run

- [ ] **Verify hermetic builds**
  - [ ] Build on two different machines
  - [ ] Verify identical output (cache keys match)
  - [ ] Verify lock file prevents version drift

- [ ] **Test cache hit rate**
  - [ ] Run 10 PRs that don't modify deps
  - [ ] Verify all use same cached image
  - [ ] Expected: 95%+ cache hit rate

---

### 📝 Phase 10: Documentation (30 minutes)

- [ ] **Create tools/ci/docker/README.md**
  ```markdown
  # Xibalba Dev Environment Container
  
  ## Overview
  
  Hermetic dev environment for CI using rules_distroless + rules_oci.
  
  ## Structure
  
  - `packages.yaml` - Package manifest (what to install)
  - `packages.lock.json` - Version lock file (exact versions)
  - `BUILD.bazel` - Bazel targets for image
  - `generate_cache_key.sh` - Compute cache key
  - `push.sh` - Push image to GHCR
  
  ## Usage
  
  ### Update Packages
  
  1. Edit `packages.yaml`
  2. Run `bazel run //tools/ci/docker:lock`
  3. Commit `packages.lock.json`
  4. CI will rebuild image automatically
  
  ### Build Locally
  
  ```bash
  # Build image
  bazel build //tools/ci/docker:xibalba_dev_env
  
  # Load into Docker
  bazel build //tools/ci/docker:xibalba_dev_env_tarball
  docker load < bazel-bin/tools/ci/docker/xibalba_dev_env_tarball/tarball.tar
  
  # Run container
  docker run -it -v $(pwd):/workspace -w /workspace xibalba-dev:local
  ```
  
  ### Push to GHCR
  
  ```bash
  # Login
  echo $GITHUB_TOKEN | docker login ghcr.io -u jmalicki --password-stdin
  
  # Push with cache key tags
  bazel run //tools/ci/docker:push
  ```
  
  ## Cache Key
  
  Generated from:
  - `MODULE.bazel.lock` - Bazel dependencies
  - `packages.lock.json` - Apt package versions
  
  When either changes, cache key changes → new image built.
  
  ## Benefits
  
  - ✅ Hermetic (versions in lock file)
  - ✅ Reproducible (lock file in git)
  - ✅ Fast CI (pull cached image vs apt install)
  - ✅ Cross-branch caching (same key = same image)
  ```

- [ ] **Update main README.md**
  - [ ] Add section on CI container:
    ```markdown
    ### CI Container Image
    
    CI uses a cached container image with all dependencies pre-installed:
    - Built with rules_distroless (hermetic)
    - Cached in GitHub Container Registry
    - Shared across all PRs (95%+ cache hit rate)
    - Saves ~2-3 minutes per CI run
    
    See: [tools/ci/docker/README.md](tools/ci/docker/README.md)
    ```

---

## Complete BUILD.bazel Example

```python
# tools/ci/docker/BUILD.bazel

load("@rules_distroless//apt:defs.bzl", "dpkg_layer")
load("@rules_oci//oci:defs.bzl", "oci_image", "oci_push", "oci_tarball")

# ============================================================================
# Lock File Management
# ============================================================================

# Update lock file when packages.yaml changes
alias(
    name = "lock",
    actual = "@ubuntu_packages//:lock",
)

# ============================================================================
# Package Layers (from apt)
# ============================================================================

# Layer 1: Build tools
dpkg_layer(
    name = "build_tools_layer",
    dpkgs = [
        "@ubuntu_packages//clang-20",
        "@ubuntu_packages//libbpf-dev",
        "@ubuntu_packages//libelf-dev",
        "@ubuntu_packages//linux-headers-generic",
    ],
)

# Layer 2: Filesystem tools
dpkg_layer(
    name = "filesystem_tools_layer",
    dpkgs = [
        "@ubuntu_packages//e2fsprogs",
        "@ubuntu_packages//xfsprogs",
        "@ubuntu_packages//btrfs-progs",
        "@ubuntu_packages//zfsutils-linux",
        "@ubuntu_packages//f2fs-tools",
        "@ubuntu_packages//nilfs-tools",
        "@ubuntu_packages//nfs-common",
        "@ubuntu_packages//qemu-system-x86-64",
    ],
)

# Layer 3: Utilities
dpkg_layer(
    name = "utils_layer",
    dpkgs = [
        "@ubuntu_packages//jq",
        "@ubuntu_packages//curl",
        "@ubuntu_packages//ca-certificates",
    ],
)

# ============================================================================
# OCI Images
# ============================================================================

# Minimal build environment
oci_image(
    name = "xibalba_build_env",
    base = "@ubuntu_base",
    tars = [
        ":build_tools_layer",
        ":utils_layer",
    ],
)

# Full dev environment (build + filesystem tools)
oci_image(
    name = "xibalba_dev_env",
    base = ":xibalba_build_env",
    tars = [":filesystem_tools_layer"],
    entrypoint = ["/bin/bash"],
)

# Tarball for local testing
oci_tarball(
    name = "xibalba_dev_env_tarball",
    image = ":xibalba_dev_env",
    repo_tags = ["xibalba-dev:local"],
)

# Push to GHCR
oci_push(
    name = "push_dev_env",
    image = ":xibalba_dev_env",
    repository = "ghcr.io/jmalicki/xibalba-dev-env",
    remote_tags = ["latest"],
)

# ============================================================================
# Utilities
# ============================================================================

# Generate cache key from lock files
sh_binary(
    name = "generate_cache_key",
    srcs = ["generate_cache_key.sh"],
    data = [
        "//:MODULE.bazel.lock",
        ":packages.lock.json",
    ],
)

# Push with cache key tags
sh_binary(
    name = "push",
    srcs = ["push.sh"],
    data = [
        ":generate_cache_key",
        ":push_dev_env",
    ],
)
```

---

## Complete MODULE.bazel Changes

```python
# Add after existing rules_oci dependency:

# Hermetic Debian package management
bazel_dep(name = "rules_distroless", version = "0.3.7")

# Configure apt package sources
apt = use_extension("@rules_distroless//apt:extensions.bzl", "apt")
apt.install(
    name = "ubuntu_packages",
    lock = "//tools/ci/docker:packages.lock.json",
    manifest = "//tools/ci/docker:packages.yaml",
)
use_repo(apt, "ubuntu_packages")
```

---

## Timeline

**Total Estimated Time:** 1-2 days

| Phase | Task | Time | Running Total |
|-------|------|------|---------------|
| 0 | Add dependency | 15 min | 15 min |
| 1 | Package manifest | 30 min | 45 min |
| 2 | OCI images | 45 min | 1.5 hours |
| 3 | Cache key | 30 min | 2 hours |
| 4 | Push setup | 45 min | 2.75 hours |
| 5 | GH Actions build | 1 hour | 3.75 hours |
| 6 | Update CI | 1 hour | 4.75 hours |
| 7 | Testing | 1-2 hours | 6-7 hours |
| 8 | Cache key in CI | 30 min | 6.5-7.5 hours |
| 9 | Measurement | 30 min | 7-8 hours |
| 10 | Documentation | 30 min | 7.5-8.5 hours |

**Working time:** 7.5-8.5 hours (~1-2 days with breaks)

---

## Success Criteria

- [ ] ✅ rules_distroless dependency added to MODULE.bazel
- [ ] ✅ packages.yaml created with all dependencies
- [ ] ✅ packages.lock.json generated and committed
- [ ] ✅ dpkg_layer targets build successfully
- [ ] ✅ oci_image builds locally
- [ ] ✅ Image loads in Docker and works
- [ ] ✅ Cache key generates consistently
- [ ] ✅ Image pushes to GHCR
- [ ] ✅ CI workflow builds image on dep changes
- [ ] ✅ CI workflow skips build when cached
- [ ] ✅ CI jobs use container instead of apt-get
- [ ] ✅ All CI tests pass with container
- [ ] ✅ Time savings measured (expect 2-3 min)
- [ ] ✅ Cross-branch caching verified
- [ ] ✅ Documentation complete

---

## Advantages Over Dockerfile

| Feature | Dockerfile | rules_distroless |
|---------|-----------|------------------|
| **Hermetic** | ❌ No | ✅ Yes |
| **Reproducible** | ⚠️ Tags only | ✅ Lock file |
| **Bazel-native** | ❌ No | ✅ Yes |
| **Version pinning** | ❌ Manual | ✅ Automatic |
| **Bazel caching** | ❌ No | ✅ Yes |
| **Setup time** | 1-2 hours | 1-2 days |
| **Docker daemon** | ✅ Needed | ❌ Optional |
| **Maintenance** | ❌ Us | ✅ Google |

---

## Next Steps

1. [ ] Review this implementation plan
2. [ ] Start with Phase 0 (add dependency)
3. [ ] Work through checklist sequentially
4. [ ] Test at each phase
5. [ ] Document learnings

---

*Implementation plan created: October 12, 2025*  
*Using: rules_distroless (Google Container Tools)*  
*Branch: investigate/docker-cache-strategy*

