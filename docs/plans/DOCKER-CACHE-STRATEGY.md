# Docker Image Caching Strategy for CI Dependencies

## Executive Summary

**Goal:** Cache CI dependencies in a Docker image using `rules_oci`, with GitHub Actions caching across branches/PRs.

**Key Insight:** Bazel + rules_oci + GitHub Container Registry (GHCR) = hermetic, cacheable dev environment.

**Benefits:**
- ⚡ **Faster CI** - Download cached image instead of `apt install` every run
- 💰 **Lower costs** - Less CI time
- 🔄 **Cross-branch caching** - One image serves all PRs
- 🔒 **Hermetic** - Bazel manages exact versions
- 📦 **Versioned** - Cache key from Bazel lockfile

**Status:** ✅ FEASIBLE - All pieces exist, need integration

---

## Current State Analysis

### What We Do Now (SLOW):

Every CI job runs:
```yaml
- name: Install dependencies
  run: |
    sudo apt-get update          # ~10s
    sudo apt-get install -y \    # ~30-60s
      clang-20 \
      libbpf-dev \
      libelf-dev \
      linux-headers-generic
```

**Per-job cost:** ~40-70 seconds  
**CI jobs:** 3-4 per run  
**Total waste:** ~2-4 minutes per CI run

### Dependencies We Install:

**Build Dependencies (from ci.yml):**
- clang-20
- libbpf-dev
- libelf-dev
- linux-headers-generic

**VM Dependencies (from packaging/BUILD.bazel):**
- e2fsprogs (ext4)
- xfsprogs (XFS)
- btrfs-progs (btrfs)
- zfsutils-linux (ZFS)
- f2fs-tools
- nilfs-tools
- nfs-common
- qemu-system-x86 (for VM tests)

**Total:** ~15-20 packages that rarely change!

---

## Project Organization

### Directory Structure:

```
tools/
├── ci/                          # CI/CD infrastructure
│   ├── docker/                  # Container images for CI
│   │   ├── BUILD.bazel         # rules_oci definitions
│   │   ├── Dockerfile          # Simple approach
│   │   ├── build_packages.txt  # Build dependencies
│   │   ├── filesystem_packages.txt  # FS tool dependencies
│   │   ├── install_packages.sh # Layer generator
│   │   └── generate_cache_key.sh   # Cache key from deps
│   └── BUILD.bazel             # CI-related tooling
├── analyze-bugs.sh
├── analyze-progress.sh
└── ... (other tools)
```

**Rationale:**
- `tools/ci/` clearly marks CI/CD infrastructure
- `tools/ci/docker/` is specifically for container images
- Separates CI concerns from general tooling
- Follows common OSS patterns (e.g., `.github/workflows`, `tools/ci/`)

## Proposed Architecture

### Three-Layer Approach:

```
Layer 1: Base OS (ubuntu:24.04)
         ↓
Layer 2: Build Tools (cached, rarely changes)
         - clang-20
         - libbpf-dev
         - libelf-dev
         - linux-headers-generic
         ↓
Layer 3: Filesystem Tools (cached, rarely changes)
         - e2fsprogs, xfsprogs, btrfs-progs
         - zfsutils-linux, f2fs-tools, etc.
         ↓
Runtime: Mount Xibalba code (changes every commit)
```

**Cache Strategy:**
- Layers 1-3: Push to GHCR, cache across all branches
- Runtime: Mount source code (not in image)

---

## Implementation Plan

### Phase 1: Research & Design ✅ (THIS DOCUMENT)

- [x] Analyze current CI dependencies
- [x] Research rules_oci capabilities
- [x] Research GitHub Actions caching
- [x] Design layered cache strategy
- [ ] Validate cache key generation approach
- [ ] Document pros/cons

### Phase 2: rules_oci Integration (2-3 hours)

#### Step 2.1: Create Base Image Definition
- [ ] Create `tools/ci/docker/BUILD.bazel`
- [ ] Define `oci_image` for build tools layer:
  ```python
  load("@rules_oci//oci:defs.bzl", "oci_image", "oci_tarball")
  
  # Layer 1: Base Ubuntu
  # (Already have this: ubuntu_base from MODULE.bazel)
  
  # Layer 2: Build tools
  oci_image(
      name = "build_tools_image",
      base = "@ubuntu_base",
      entrypoint = ["/bin/bash"],
      tars = [":build_tools_layer"],
  )
  ```

#### Step 2.2: Create APT Package Installation Layer
- [ ] Research best approach for apt in rules_oci:
  - Option A: Use `genrule` to run `apt-get` and create tar
  - Option B: Use `rules_pkg` to create deb bundle
  - Option C: Use `oci_image` with custom layer
- [ ] Create tar layer with installed packages:
  ```python
  genrule(
      name = "build_tools_layer",
      outs = ["build_tools.tar"],
      cmd = """
          # Create temp rootfs
          mkdir -p tmp/rootfs
          # Use debootstrap or apt-get with --root
          # Install: clang-20, libbpf-dev, libelf-dev, linux-headers-generic
          # Create tar: tar -czf $@ -C tmp/rootfs .
      """,
  )
  ```

#### Step 2.3: Add Filesystem Tools Layer
- [ ] Create second layer for filesystem tools:
  ```python
  oci_image(
      name = "test_tools_image",
      base = ":build_tools_image",
      tars = [":filesystem_tools_layer"],
  )
  ```
- [ ] Install: e2fsprogs, xfsprogs, btrfs-progs, zfsutils-linux, etc.

#### Step 2.4: Push to Registry
- [ ] Define `oci_push` target:
  ```python
  oci_push(
      name = "push_test_tools",
      image = ":test_tools_image",
      repository = "ghcr.io/jmalicki/xibalba-dev-env",
      remote_tags = ["latest", "v1"],
  )
  ```

### Phase 3: Cache Key Generation (1-2 hours)

#### Step 3.1: Generate Hash from Dependencies
- [ ] Create `tools/ci/docker/generate_cache_key.sh`:
  ```bash
  #!/bin/bash
  # Generate cache key from Bazel lockfile + package list
  cat MODULE.bazel.lock packaging/BUILD.bazel | sha256sum | cut -d' ' -f1
  ```
- [ ] Test cache key generation:
  - [ ] Verify key is stable when deps don't change
  - [ ] Verify key changes when deps change

#### Step 3.2: Tag Images with Cache Key
- [ ] Modify `oci_push` to use cache key as tag:
  ```python
  genrule(
      name = "cache_key",
      outs = ["cache_key.txt"],
      cmd = "$(location :generate_cache_key.sh) > $@",
      tools = [":generate_cache_key.sh"],
  )
  ```

### Phase 4: GitHub Actions Integration (2-3 hours)

#### Step 4.1: Add Docker Image Build Workflow
- [ ] Create `.github/workflows/build-dev-image.yml`:
  ```yaml
  name: Build Dev Image
  
  on:
    push:
      paths:
        - 'MODULE.bazel'
        - 'MODULE.bazel.lock'
        - 'packaging/BUILD.bazel'
        - 'tools/ci/docker/**'
    workflow_dispatch:  # Manual trigger
  
  jobs:
    build-image:
      runs-on: ubuntu-24.04
      permissions:
        contents: read
        packages: write  # Push to GHCR
      
      steps:
      - uses: actions/checkout@v4
      
      - name: Setup Bazelisk
        uses: bazelbuild/setup-bazelisk@v3
      
      - name: Generate cache key
        id: cache-key
        run: |
          CACHE_KEY=$(bazel run //tools/ci/docker:generate_cache_key)
          echo "key=$CACHE_KEY" >> $GITHUB_OUTPUT
      
      - name: Check if image exists
        id: check-image
        run: |
          # Try to pull image with this cache key
          if docker pull ghcr.io/jmalicki/xibalba-dev-env:${{ steps.cache-key.outputs.key }}; then
            echo "exists=true" >> $GITHUB_OUTPUT
          else
            echo "exists=false" >> $GITHUB_OUTPUT
          fi
      
      - name: Build and push image (if not exists)
        if: steps.check-image.outputs.exists == 'false'
        run: |
          bazel run //tools/ci/docker:push_test_tools
      
      - name: Tag as latest
        run: |
          docker tag ghcr.io/jmalicki/xibalba-dev-env:${{ steps.cache-key.outputs.key }} \
                     ghcr.io/jmalicki/xibalba-dev-env:latest
          docker push ghcr.io/jmalicki/xibalba-dev-env:latest
  ```

#### Step 4.2: Update CI to Use Cached Image
- [ ] Modify `.github/workflows/ci.yml`:
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
    
    # NO MORE apt-get install! Dependencies already in container
    
    - name: Setup Bazelisk
      uses: bazelbuild/setup-bazelisk@v3
    
    - name: Run tests
      run: bazel test //common:state_tracker_test
  ```

#### Step 4.3: Add Fallback for Fresh Builds
- [ ] Add fallback when image doesn't exist:
  ```yaml
  - name: Install dependencies (fallback)
    if: failure()  # If container pull fails
    run: |
      sudo apt-get update
      sudo apt-get install -y clang-20 libbpf-dev libelf-dev
  ```

### Phase 5: Cross-Branch Caching Strategy (1 hour)

#### Step 5.1: GitHub Container Registry Setup
- [ ] Enable GHCR for repository:
  - [ ] Go to repository Settings → Packages
  - [ ] Enable "Inherit access from repository"
  - [ ] Allow read access for public PRs

#### Step 5.2: Cache Key Strategy
- [ ] Implement cache key hierarchy:
  ```
  ghcr.io/jmalicki/xibalba-dev-env:<cache-key>     # Specific version
  ghcr.io/jmalicki/xibalba-dev-env:main            # Main branch
  ghcr.io/jmalicki/xibalba-dev-env:latest          # Fallback
  ```

- [ ] Pull strategy in CI:
  ```yaml
  - name: Login to GHCR
    uses: docker/login-action@v3
    with:
      registry: ghcr.io
      username: ${{ github.actor }}
      password: ${{ secrets.GITHUB_TOKEN }}
  
  - name: Pull dev image with fallback
    run: |
      CACHE_KEY=$(cat tools/ci/docker/cache_key.txt)
      docker pull ghcr.io/jmalicki/xibalba-dev-env:${CACHE_KEY} || \
      docker pull ghcr.io/jmalicki/xibalba-dev-env:main || \
      docker pull ghcr.io/jmalicki/xibalba-dev-env:latest
  ```

### Phase 6: Testing & Validation (1-2 hours)

#### Step 6.1: Test Image Build
- [ ] Build image locally:
  ```bash
  bazel build //tools/ci/docker:test_tools_image
  bazel run //tools/ci/docker:push_test_tools  # Dry run
  ```
- [ ] Verify all dependencies present in image
- [ ] Test running Bazel inside container

#### Step 6.2: Test CI Workflow
- [ ] Create test PR with image
- [ ] Verify image pull works
- [ ] Verify tests run successfully
- [ ] Measure time savings

#### Step 6.3: Test Cache Hit/Miss
- [ ] Modify MODULE.bazel (should rebuild image)
- [ ] Don't modify deps (should use cache)
- [ ] Verify cache key logic works

### Phase 7: Optimization & Documentation (1 hour)

#### Step 7.1: Optimize Image Size
- [ ] Use multi-stage build to minimize size
- [ ] Remove unnecessary files after apt install
- [ ] Verify image < 500MB

#### Step 7.2: Documentation
- [ ] Document cache key generation
- [ ] Document how to rebuild image
- [ ] Document fallback strategy
- [ ] Add to README.md

---

## Technical Details

### Cache Key Generation Strategy

**Hash Inputs:**
1. `MODULE.bazel.lock` - Bazel dependencies
2. `packaging/BUILD.bazel` - Package list
3. `tools/ci/docker/Dockerfile.packages` (if we create one) - APT packages

**Generation:**
```bash
#!/bin/bash
# tools/ci/docker/generate_cache_key.sh
cat MODULE.bazel.lock packaging/BUILD.bazel | sha256sum | awk '{print $1}'
```

**Usage:**
```bash
CACHE_KEY=$(bazel run //tools/ci/docker:generate_cache_key)
IMAGE_TAG="ghcr.io/jmalicki/xibalba-dev-env:${CACHE_KEY}"
```

### GitHub Actions Cache Capabilities

**GitHub Container Registry (GHCR) Features:**
- ✅ Cross-branch access (with permissions)
- ✅ Unlimited storage for public repos
- ✅ 500MB-10GB per image
- ✅ Tag-based versioning
- ✅ Automatic cleanup policies
- ✅ No extra cost

**Cache Scopes:**
- ✅ Same repository: All branches can read
- ✅ Forks: Can read public images
- ✅ PRs: Can read from base branch

### rules_oci Capabilities

**What rules_oci Provides:**
- ✅ Hermetic image builds (no Docker daemon needed!)
- ✅ Bazel-native (cached, incremental)
- ✅ OCI-compliant images
- ✅ Push to any registry (GHCR, Docker Hub, etc.)
- ✅ Multi-platform support

**What rules_oci Does NOT Provide:**
- ❌ Built-in apt-get support (need custom layer)
- ❌ Dockerfile compatibility (different approach)
- ❌ Docker buildx caching

### Alternative Approaches Considered

#### Option A: rules_oci (RECOMMENDED)
**Pros:**
- ✅ Hermetic, Bazel-native
- ✅ No Docker daemon needed
- ✅ Better caching (Bazel)
- ✅ Cross-platform

**Cons:**
- ⚠️ More complex APT integration
- ⚠️ Less documentation
- ⚠️ Steeper learning curve

#### Option B: Docker Buildx + GitHub Actions Cache
**Pros:**
- ✅ Well-documented
- ✅ Easy APT integration
- ✅ Dockerfile familiar

**Cons:**
- ❌ Not hermetic
- ❌ Separate from Bazel
- ❌ Need Docker daemon
- ❌ More complex caching

#### Option C: Pre-built Image (Manual)
**Pros:**
- ✅ Simplest to use
- ✅ Fastest CI

**Cons:**
- ❌ Manual updates
- ❌ No automation
- ❌ No cache key logic

**Recommendation:** Use Option A (rules_oci) for hermetic builds

---

## Detailed Implementation Checklist

### 📦 Phase 0: Add rules_distroless Dependency (15 minutes)

- [ ] **Add to MODULE.bazel**
  - [ ] Open `MODULE.bazel`
  - [ ] Add after existing `bazel_dep` entries:
    ```python
    # Hermetic Debian package management for dev environment
    bazel_dep(name = "rules_distroless", version = "0.3.7")
    ```
  - [ ] Update lockfile: `bazel mod update`

- [ ] **Verify dependency loads**
  - [ ] Run: `bazel query @rules_distroless//...`
  - [ ] Should see rules_distroless targets
  - [ ] No errors

### 🔧 Phase 1: Setup Package Manifest and Lock File (30-45 minutes)

- [ ] **Create directory structure**
  - [ ] `mkdir -p tools/ci/docker`
  - [ ] Create `tools/ci/docker/BUILD.bazel`
  - [ ] Create `tools/ci/docker/README.md`
  - [ ] Create `tools/ci/BUILD.bazel` (if doesn't exist)

- [ ] **Create package manifest (YAML)**
  - [ ] Create `tools/ci/docker/packages.yaml`:
    ```yaml
    version: 1
    
    sources:
      - channel: ubuntu noble amd64
        url: http://archive.ubuntu.com/ubuntu
    
    archs:
      - amd64
    
    packages:
      # Build dependencies
      - clang-20
      - libbpf-dev
      - libelf-dev
      - linux-headers-generic
      
      # Filesystem tools
      - e2fsprogs
      - xfsprogs
      - btrfs-progs
      - zfsutils-linux
      - f2fs-tools
      - nilfs-tools
      - nfs-common
      - qemu-system-x86-64
    ```

- [ ] **Register apt extension in MODULE.bazel**
  - [ ] Add after rules_distroless dependency:
    ```python
    apt = use_extension("@rules_distroless//apt:extensions.bzl", "apt")
    apt.install(
        name = "ubuntu_packages",
        lock = "//tools/ci/docker:packages.lock.json",
        manifest = "//tools/ci/docker:packages.yaml",
    )
    use_repo(apt, "ubuntu_packages")
    ```

- [ ] **Generate lock file**
  - [ ] Run: `bazel run //tools/ci/docker:lock`
  - [ ] This creates `packages.lock.json`
  - [ ] Commit lock file to git
  - [ ] Lock file pins exact package versions

- [ ] **Create dpkg layers with rules_distroless**
  - [ ] Add to `tools/ci/docker/BUILD.bazel`:
    ```python
    load("@rules_distroless//apt:defs.bzl", "dpkg_layer")
    load("@rules_oci//oci:defs.bzl", "oci_image", "oci_push", "oci_tarball")
    
    # Build tools layer (from apt packages)
    dpkg_layer(
        name = "build_tools_layer",
        dpkgs = [
            "@ubuntu_packages//clang-20",
            "@ubuntu_packages//libbpf-dev",
            "@ubuntu_packages//libelf-dev",
            "@ubuntu_packages//linux-headers-generic",
        ],
    )
    
    # Filesystem tools layer (separate for better caching)
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

- [ ] **Define OCI images**
  - [ ] Add to `tools/ci/docker/BUILD.bazel`:
    ```python
    # Build tools image
    oci_image(
        name = "xibalba_build_env",
        base = "@ubuntu_base",
        tars = [":build_tools_layer"],
    )
    
    # Full dev image (build + filesystem tools)
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
    
    # Lock file updater
    alias(
        name = "lock",
        actual = "@ubuntu_packages//:lock",
    )
    ```

### 🔑 Phase 2: Cache Key Generation

- [ ] **Create cache key generator**
  - [ ] Create `tools/ci/docker/generate_cache_key.sh`:
    ```bash
    #!/bin/bash
    # Generate cache key from dependencies
    set -euo pipefail
    
    # Hash MODULE.bazel.lock (Bazel deps)
    BAZEL_HASH=$(sha256sum MODULE.bazel.lock | cut -d' ' -f1)
    
    # Hash package lists
    PKG_HASH=$(cat tools/ci/docker/*_packages.txt | sort | sha256sum | cut -d' ' -f1)
    
    # Combine hashes
    echo "${BAZEL_HASH:0:8}-${PKG_HASH:0:8}"
    ```

- [ ] **Add Bazel target**
  - [ ] Add to `tools/ci/docker/BUILD.bazel`:
    ```python
    sh_binary(
        name = "generate_cache_key",
        srcs = ["generate_cache_key.sh"],
        data = [
            "//:MODULE.bazel.lock",
            ":build_packages.txt",
            ":filesystem_packages.txt",
        ],
    )
    ```

- [ ] **Test cache key stability**
  - [ ] Run twice, verify same output
  - [ ] Modify MODULE.bazel.lock, verify different output
  - [ ] Modify packages.txt, verify different output

### 🐳 Phase 3: GitHub Container Registry Setup

- [ ] **Enable GHCR for repository**
  - [ ] Go to https://github.com/jmalicki/xibalba/settings/packages
  - [ ] Enable "Package creation" for GHCR
  - [ ] Set visibility: Public (or Private with PAT)

- [ ] **Configure repository permissions**
  - [ ] Settings → Actions → General
  - [ ] Workflow permissions: "Read and write permissions"
  - [ ] Enable "Allow GitHub Actions to create and approve pull requests"

- [ ] **Test GHCR access**
  - [ ] Manually push test image:
    ```bash
    echo $GITHUB_TOKEN | docker login ghcr.io -u jmalicki --password-stdin
    docker tag test ghcr.io/jmalicki/xibalba-dev-env:test
    docker push ghcr.io/jmalicki/xibalba-dev-env:test
    ```

### 📦 Phase 4: Update Push Configuration (30 minutes)

- [ ] **Add GHCR push target**
  - [ ] Update `tools/ci/docker/BUILD.bazel`:
    ```python
    # Push to GitHub Container Registry
    oci_push(
        name = "push_dev_env",
        image = ":xibalba_dev_env",
        repository = "ghcr.io/jmalicki/xibalba-dev-env",
        remote_tags = ["latest"],  # Will override with cache key in CI
    )
    ```

- [ ] **Create push script with cache key and multi-tag support**
  - [ ] Create `tools/ci/docker/push_with_cache_key.sh`:
    ```bash
    #!/bin/bash
    # Push dev environment image with cache key tag
    set -euo pipefail
    
    CACHE_KEY=$(bazel run //tools/ci/docker:generate_cache_key)
    echo "📦 Cache key: $CACHE_KEY"
    
    # Build the image (uses Bazel cache)
    echo "🔨 Building image..."
    bazel build //tools/ci/docker:xibalba_dev_env
    
    # Tag with cache key
    echo "🏷️  Tagging ghcr.io/jmalicki/xibalba-dev-env:${CACHE_KEY}"
    bazel run //tools/ci/docker:push_dev_env -- --tag ${CACHE_KEY}
    
    # Also tag as latest
    echo "🏷️  Tagging ghcr.io/jmalicki/xibalba-dev-env:latest"
    bazel run //tools/ci/docker:push_dev_env -- --tag latest
    
    echo "✅ Image pushed successfully"
    ```
  
  - [ ] Make executable: `chmod +x tools/ci/docker/push_with_cache_key.sh`

- [ ] **Add to BUILD.bazel**
  - [ ] Add script as runnable target:
    ```python
    sh_binary(
        name = "push",
        srcs = ["push_with_cache_key.sh"],
        data = [
            ":generate_cache_key",
            ":push_dev_env",
        ],
    )
    ```

### ⚙️ Phase 5: GitHub Actions Workflow

- [ ] **Create image build workflow**
  - [ ] Create `.github/workflows/build-dev-image.yml` (see template above)
  - [ ] Test workflow triggers on MODULE.bazel changes
  - [ ] Test workflow manual dispatch

- [ ] **Update CI workflow to use image**
  - [ ] Modify `.github/workflows/ci.yml`:
    ```yaml
    unit-tests:
      runs-on: ubuntu-24.04
      container:
        image: ghcr.io/jmalicki/xibalba-dev-env:latest
        credentials:
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}
      
      steps:
      - uses: actions/checkout@v4
      
      # Dependencies already installed in container!
      # No apt-get update/install needed
      
      - name: Setup Bazelisk
        uses: bazelbuild/setup-bazelisk@v3
      
      - name: Run tests
        run: bazel test //common:state_tracker_test
    ```

- [ ] **Keep fallback for robustness**
  - [ ] Add step to install deps if container fails
  - [ ] Test fallback scenario

### 🧪 Phase 6: Testing & Validation

- [ ] **Local testing**
  - [ ] Build image locally:
    ```bash
    bazel build //tools/ci/docker:xibalba_dev_env_tarball
    docker load < bazel-bin/tools/ci/docker/xibalba_dev_env_tarball/tarball.tar
    ```
  - [ ] Test running Bazel in container:
    ```bash
    docker run -v $(pwd):/workspace -w /workspace xibalba-dev:local \
      bazel test //common:state_tracker_test
    ```
  - [ ] Verify all dependencies work

- [ ] **CI testing**
  - [ ] Create test PR
  - [ ] Verify image builds on first run
  - [ ] Verify image cached on second run
  - [ ] Measure time savings

- [ ] **Cross-branch testing**
  - [ ] Create PR from different branch
  - [ ] Verify it uses cached image
  - [ ] Confirm no rebuild needed

### 📊 Phase 7: Measure & Optimize

- [ ] **Measure improvements**
  - [ ] Before: Record CI time for dependency install
  - [ ] After: Record CI time with cached image
  - [ ] Calculate savings (expect 40-70s → 5-10s)

- [ ] **Optimize image size**
  - [ ] Check image size: `docker images | grep xibalba`
  - [ ] Target: < 500MB
  - [ ] Clean up apt cache in layer:
    ```bash
    apt-get clean
    rm -rf /var/lib/apt/lists/*
    ```

- [ ] **Optimize layer count**
  - [ ] Combine layers where possible
  - [ ] Keep build tools separate from filesystem tools (different change frequencies)

### 📝 Phase 8: Documentation

- [ ] **Create documentation**
  - [ ] Update `tools/ci/docker/README.md`:
    - [ ] Explain architecture
    - [ ] Document cache key generation
    - [ ] Explain when image rebuilds
    - [ ] How to manually rebuild
  
- [ ] **Update main README**
  - [ ] Add section on Docker-based CI
  - [ ] Document how contributors can test locally with image

- [ ] **Create troubleshooting guide**
  - [ ] What if image pull fails
  - [ ] How to force rebuild
  - [ ] How to test locally

---

## Alternative: Simpler Docker Buildx Approach

If rules_oci proves too complex, here's a simpler approach:

### Simple Dockerfile Approach (1-2 hours total)

- [ ] **Create Dockerfile**
  - [ ] `tools/ci/docker/Dockerfile`:
    ```dockerfile
    FROM ubuntu:24.04
    
    RUN apt-get update && apt-get install -y \
        clang-20 \
        libbpf-dev \
        libelf-dev \
        linux-headers-generic \
        e2fsprogs \
        xfsprogs \
        btrfs-progs \
        zfsutils-linux \
        f2fs-tools \
        nilfs-tools \
        nfs-common \
        qemu-system-x86 \
      && apt-get clean \
      && rm -rf /var/lib/apt/lists/*
    
    WORKDIR /workspace
    ```

- [ ] **Add build workflow**
  - [ ] `.github/workflows/build-dev-image.yml`:
    ```yaml
    - name: Generate cache key
      id: cache-key
      run: |
        HASH=$(cat MODULE.bazel.lock tools/ci/docker/Dockerfile | sha256sum | cut -d' ' -f1)
        echo "key=${HASH:0:12}" >> $GITHUB_OUTPUT
    
    - name: Set up Docker Buildx
      uses: docker/setup-buildx-action@v3
    
    - name: Login to GHCR
      uses: docker/login-action@v3
      with:
        registry: ghcr.io
        username: ${{ github.actor }}
        password: ${{ secrets.GITHUB_TOKEN }}
    
    - name: Build and push
      uses: docker/build-push-action@v5
      with:
        context: tools/ci/docker
        push: true
        tags: |
          ghcr.io/jmalicki/xibalba-dev-env:${{ steps.cache-key.outputs.key }}
          ghcr.io/jmalicki/xibalba-dev-env:latest
        cache-from: type=gha
        cache-to: type=gha,mode=max
    ```

**Pros of Dockerfile approach:**
- ✅ Much simpler
- ✅ Familiar (Dockerfile)
- ✅ Well-documented
- ✅ GitHub Actions cache built-in

**Cons:**
- ❌ Not hermetic (not Bazel-managed)
- ❌ Need Docker daemon
- ❌ Separate from Bazel ecosystem

---

## Recommended Approach

### Use rules_distroless (Hermetic + Bazel-Native)

**Phase 1:** Implement rules_distroless (1-2 days)
- Hermetic from day one
- Bazel-native caching
- Integrates perfectly with rules_oci
- Google-maintained, production-ready

**Why rules_distroless:**

1. **Hermetic** - Package versions pinned in lock file
2. **Bazel-native** - Everything in Bazel, no Docker daemon
3. **Reproducible** - Lock file checked into git
4. **Cacheable** - Bazel caching + GitHub Actions
5. **Official** - Maintained by Google Container Tools
6. **Future-proof** - Modern bzlmod approach

**Fallback:** Dockerfile approach documented in appendix if rules_distroless proves too complex

---

## Expected Results

### Time Savings

**Current (no cache):**
```
apt-get update:  10s
apt-get install: 40-60s
Total per job:   50-70s
× 3 jobs:        150-210s (2.5-3.5 minutes)
```

**With Docker cache:**
```
docker pull:     5-10s (cached layers)
Total per job:   5-10s
× 3 jobs:        15-30s
```

**Savings: ~2-3 minutes per CI run** 🎉

### Cache Hit Rate

**Expected:**
- 95%+ cache hits (deps rarely change)
- Cache invalidates only when:
  - MODULE.bazel.lock changes (Bazel dep update)
  - Package list changes (add new tool)
  - Dockerfile changes

**Rebuild frequency:**
- ~Once per month (normal development)
- ~Once per week (active dependency updates)

---

## Risks & Mitigations

### Risk 1: Image Pull Failure
**Mitigation:**
- Keep fallback apt-get install in CI
- If container pull fails, fall back to old method

### Risk 2: Stale Cache
**Mitigation:**
- Tag images with cache key (exact versions)
- Manual rebuild trigger in workflow
- Automatic rebuild on schedule (weekly)

### Risk 3: GHCR Storage Limits
**Mitigation:**
- Keep only last 5-10 versions
- Automatic cleanup of old tags
- Images are small (~300-500MB)

### Risk 4: Complex Debugging
**Mitigation:**
- Document thoroughly
- Provide local test instructions
- Keep simple Dockerfile approach

---

## Success Criteria

- [ ] ✅ CI runs 2+ minutes faster
- [ ] ✅ No apt-get install in CI logs
- [ ] ✅ Cache works across branches/PRs
- [ ] ✅ Image rebuilds when deps change
- [ ] ✅ Image doesn't rebuild when deps same
- [ ] ✅ Fallback works if image unavailable
- [ ] ✅ Documentation complete
- [ ] ✅ Team can rebuild image manually

---

## Implementation Timeline

### Quick Win (Dockerfile Approach):
- **Phase 1-2:** Research & Design - ✅ DONE (this doc)
- **Phase 3-4:** Implement Dockerfile + scripts - 1 hour
- **Phase 5:** GitHub Actions integration - 1 hour  
- **Phase 6:** Testing - 30 minutes
- **Phase 7:** Documentation - 30 minutes

**Total: 3-4 hours for working solution**

### Full Solution (rules_oci Approach):
- **Additional time:** +1-2 days for rules_oci
- **When:** After Dockerfile proves the concept

---

## Next Steps

1. **Decision:** Dockerfile or rules_oci first?
   - **Recommendation:** Start with Dockerfile

2. **Approval:** Get buy-in on approach
   
3. **Implementation:** Follow checklist above

4. **Validation:** Measure time savings

5. **Document:** Update team docs

---

## Appendix: Example Implementations

### Example: Dockerfile

```dockerfile
# tools/ci/docker/Dockerfile
FROM ubuntu:24.04

# Prevent interactive prompts
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
    qemu-system-x86 \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
```

### Example: GitHub Workflow

``yaml
# .github/workflows/build-dev-image.yml
name: Build Dev Image

on:
  push:
    paths:
      - 'MODULE.bazel.lock'
      - 'tools/ci/docker/**'
  workflow_dispatch:

jobs:
  build:
    runs-on: ubuntu-24.04
    permissions:
      contents: read
      packages: write
    
    steps:
    - uses: actions/checkout@v4
    
    - name: Generate cache key
      id: cache-key
      run: |
        HASH=$(cat MODULE.bazel.lock tools/ci/docker/Dockerfile | sha256sum | cut -d' ' -f1)
        echo "key=${HASH:0:12}" >> $GITHUB_OUTPUT
    
    - name: Login to GHCR
      uses: docker/login-action@v3
      with:
        registry: ghcr.io
        username: ${{ github.actor }}
        password: ${{ secrets.GITHUB_TOKEN }}
    
    - name: Check if image exists
      id: check
      run: |
        if docker manifest inspect ghcr.io/jmalicki/xibalba-dev-env:${{ steps.cache-key.outputs.key }} > /dev/null 2>&1; then
          echo "exists=true" >> $GITHUB_OUTPUT
        else
          echo "exists=false" >> $GITHUB_OUTPUT
        fi
    
    - name: Build and push
      if: steps.check.outputs.exists == 'false'
      run: |
        docker build -t ghcr.io/jmalicki/xibalba-dev-env:${{ steps.cache-key.outputs.key }} tools/ci/docker
        docker push ghcr.io/jmalicki/xibalba-dev-env:${{ steps.cache-key.outputs.key }}
        
        # Also tag as latest
        docker tag ghcr.io/jmalicki/xibalba-dev-env:${{ steps.cache-key.outputs.key }} \
                   ghcr.io/jmalicki/xibalba-dev-env:latest
        docker push ghcr.io/jmalicki/xibalba-dev-env:latest
```

### Example: Updated CI

```yaml
# .github/workflows/ci.yml
unit-tests:
  runs-on: ubuntu-24.04
  container:
    image: ghcr.io/jmalicki/xibalba-dev-env:latest
    credentials:
      username: ${{ github.actor }}
      password: ${{ secrets.GITHUB_TOKEN }}
  
  steps:
  - uses: actions/checkout@v4
  
  # NO apt-get install needed! 🎉
  
  - name: Setup Bazelisk
    uses: bazelbuild/setup-bazelisk@v3
  
  - name: Run tests
    run: bazel test //common:state_tracker_test
```

---

*Document created: October 12, 2025*  
*For Xibalba CI Optimization*

