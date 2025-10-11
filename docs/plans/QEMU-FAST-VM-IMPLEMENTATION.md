# Fast QEMU-based VM Testing - Implementation Plan

**Goal**: Replace slow SSH-based VM testing with fast direct QEMU execution (~10s vs ~3min)

**Reference**: See `docs/design/FAST-VM-TESTING.md` for design rationale

## Architecture Overview

```
Bazel Build:
├─ Download Ubuntu cloud image
├─ Extract kernel + initrd (hermetic)
├─ Create initramfs with custom init script
├─ Build xibalba binaries
└─ Create test rules that:
   ├─ Launch QEMU with -enable-kvm
   ├─ Share xibalba binaries via 9p
   ├─ Attach test block device
   ├─ Capture serial console output
   └─ Parse results

VM Execution:
1. Kernel boots (1s)
2. Custom init mounts 9p share (1s)
3. Format /dev/vda as test filesystem (1-2s)
4. Run xibalba-gauntlet from 9p mount (tests run...)
5. Output results to serial console
6. VM powers off
```

## Directory Structure

New files to create:

```
vm/
├── BUILD.bazel                    (updated with new rules)
├── build-scripts/                 (NEW - hermetic build scripts)
│   ├── extract-kernel.sh         (runs in container)
│   └── build-initramfs.sh        (runs in container)
├── qemu/                          (NEW - QEMU testing infrastructure)
│   ├── init.sh                   (VM init script - PID 1)
│   ├── run-qemu-test.sh          (QEMU launcher)
│   └── test-wrapper.sh           (parse results)
└── qemu_test.bzl                  (NEW - test macro)
```

**Key principle**: 
- Genrules are **simple** (just invoke scripts)
- Build logic lives in **standalone scripts**
- Scripts can be tested independently

## Phase 1: Foundation (Hermetic Kernel/Initrd)

### 1.0 Setup Build Environment
**Files**: `MODULE.bazel`, `vm/BUILD.bazel`

- [ ] Add rules_oci to MODULE.bazel
  ```python
  bazel_dep(name = "rules_oci", version = "1.7.4")
  
  oci = use_extension("@rules_oci//oci:extensions.bzl", "oci")
  oci.pull(
      name = "ubuntu_base",
      image = "ubuntu",
      tag = "24.04",
      digest = "sha256:...",  # Pin for reproducibility
  )
  use_repo(oci, "ubuntu_base")
  ```

- [ ] Create build tools container image
  **File**: `vm/BUILD.bazel`
  ```python
  load("@rules_oci//oci:defs.bzl", "oci_image", "oci_tarball")
  
  # Container with all build tools (runs build steps)
  oci_image(
      name = "build_tools_image",
      base = "@ubuntu_base",
      cmd = ["/bin/bash"],
      entrypoint = ["/bin/bash", "-c"],
  )
  
  # Install build tools in container
  oci_image(
      name = "build_tools",
      base = ":build_tools_image",
      # This runs at image build time
      run = """
          apt-get update && \
          apt-get install -y --no-install-recommends \
              binutils \
              tar \
              xz-utils \
              cpio \
              gzip \
              wget \
              ca-certificates && \
          apt-get clean && \
          rm -rf /var/lib/apt/lists/*
      """,
  )
  ```

### 1.1 Download Kernel Package
**File**: `vm/BUILD.bazel`

- [ ] Download kernel .deb package (hermetic, pinned version)
  ```python
  http_file(
      name = "ubuntu_kernel_deb",
      urls = ["http://archive.ubuntu.com/ubuntu/pool/main/l/linux/linux-image-6.8.0-40-generic_6.8.0-40.40_amd64.deb"],
      sha256 = "...",  # TODO: Get actual hash
      downloaded_file_path = "linux-image.deb",
  )
  ```

### 1.2 Extract Kernel in Container

- [ ] **Create script**: `vm/build-scripts/extract-kernel.sh`
  ```bash
  #!/bin/bash
  # Extract kernel from Ubuntu .deb package
  set -euo pipefail
  
  INPUT_DEB="$1"
  OUTPUT_FILE="$2"
  
  cd /tmp
  ar x "$INPUT_DEB" data.tar.xz
  tar xf data.tar.xz ./boot/vmlinuz-* --strip-components=2
  cp vmlinuz-* "$OUTPUT_FILE"
  ```

- [ ] **Create genrule**: `vm/BUILD.bazel`
  ```python
  # Simple genrule - just invokes script in container
  genrule(
      name = "extract_kernel",
      srcs = [
          ":ubuntu_kernel_deb",
          "build-scripts/extract-kernel.sh",
      ],
      outs = ["vmlinuz"],
      cmd = """
          docker run --rm \
              -v $(location :ubuntu_kernel_deb):/input/kernel.deb:ro \
              -v $(location build-scripts/extract-kernel.sh):/extract.sh:ro \
              -v $(@D):/output \
              bazel/vm:build_tools \
              bash /extract.sh /input/kernel.deb /output/vmlinuz
      """,
      tags = ["docker"],
  )
  ```

### 1.3 Download Filesystem Tool Packages
**File**: `vm/BUILD.bazel`

- [ ] Download filesystem tool packages
  ```python
  http_file(
      name = "e2fsprogs_deb",
      urls = ["http://archive.ubuntu.com/ubuntu/pool/main/e/e2fsprogs/e2fsprogs_1.47.0-2ubuntu1_amd64.deb"],
      sha256 = "...",
  )
  
  http_file(
      name = "xfsprogs_deb",
      urls = ["http://archive.ubuntu.com/ubuntu/pool/main/x/xfsprogs/xfsprogs_6.7.0-1_amd64.deb"],
      sha256 = "...",
  )
  
  http_file(
      name = "btrfs_progs_deb",
      urls = ["http://archive.ubuntu.com/ubuntu/pool/main/b/btrfs-progs/btrfs-progs_6.7.1-1ubuntu1_amd64.deb"],
      sha256 = "...",
  )
  ```

**Note**: Container approach handles dependencies automatically!

## Phase 2: Custom Init System

### 2.1 Create Init Script
**File**: `vm/qemu/init.sh`

- [ ] Create shell script that will run as PID 1 in VM
  ```bash
  #!/bin/sh
  # Minimal init for xibalba testing
  
  set -e
  
  echo "=== Xibalba Fast VM Init ==="
  
  # Mount essential filesystems
  mount -t proc none /proc
  mount -t sysfs none /sys
  mount -t devtmpfs none /dev
  
  # Mount 9p share with xibalba binaries
  mkdir -p /opt/xibalba
  mount -t 9p -o trans=virtio,version=9p2000.L xibalba /opt/xibalba
  
  # Add to PATH
  export PATH=/opt/xibalba/bin:$PATH
  
  # Get test parameters from kernel cmdline
  FILESYSTEM=$(cat /proc/cmdline | grep -o 'xibalba.fs=[^ ]*' | cut -d= -f2)
  DURATION=$(cat /proc/cmdline | grep -o 'xibalba.duration=[^ ]*' | cut -d= -f2 || echo 300)
  
  # Format test device
  echo "Formatting /dev/vda as ${FILESYSTEM}..."
  case "$FILESYSTEM" in
      ext4)
          mkfs.ext4 -F /dev/vda
          ;;
      xfs)
          mkfs.xfs -f /dev/vda
          ;;
      zfs)
          # ZFS setup is more complex...
          zpool create testpool /dev/vda
          ;;
      *)
          echo "Unknown filesystem: $FILESYSTEM"
          exit 1
          ;;
  esac
  
  # Mount test filesystem
  mkdir -p /test
  mount /dev/vda /test
  
  # Run xibalba tests
  echo "Running xibalba-gauntlet..."
  cd /test
  xibalba-gauntlet "$FILESYSTEM" || EXIT_CODE=$?
  
  # Output results marker
  echo "=== XIBALBA_TEST_COMPLETE ==="
  echo "EXIT_CODE=${EXIT_CODE:-0}"
  
  # Poweroff
  sync
  poweroff -f
  ```

- [ ] Make script executable
- [ ] Add to Bazel as data file

### 2.2 Build Custom Initramfs in Container

- [ ] **Create script**: `vm/build-scripts/build-initramfs.sh`
  ```bash
  #!/bin/bash
  # Build custom initramfs with filesystem tools
  set -euo pipefail
  
  INIT_SCRIPT="$1"
  E2FSPROGS_DEB="$2"
  XFSPROGS_DEB="$3"
  BTRFS_DEB="$4"
  OUTPUT_FILE="$5"
  
  cd /tmp
  mkdir -p initrd/{bin,sbin,dev,proc,sys,test,opt/xibalba}
  
  # Extract filesystem tools from .debs
  dpkg-deb -x "$E2FSPROGS_DEB" initrd/
  dpkg-deb -x "$XFSPROGS_DEB" initrd/
  dpkg-deb -x "$BTRFS_DEB" initrd/
  
  # Copy busybox and init
  cp /bin/busybox initrd/bin/
  cp "$INIT_SCRIPT" initrd/init
  chmod +x initrd/init
  
  # Create busybox symlinks
  cd initrd/bin
  for cmd in sh mount umount mkdir cat grep echo; do
      ln -s busybox "$cmd"
  done
  cd /tmp
  
  # Create initramfs
  cd initrd
  find . | cpio -o -H newc | gzip > "$OUTPUT_FILE"
  ```

- [ ] **Create genrule**: `vm/BUILD.bazel`
  ```python
  # Simple genrule - just invokes script in container
  genrule(
      name = "custom_initramfs",
      srcs = [
          "qemu/init.sh",
          "build-scripts/build-initramfs.sh",
          ":e2fsprogs_deb",
          ":xfsprogs_deb",
          ":btrfs_progs_deb",
      ],
      outs = ["initramfs.img"],
      cmd = """
          docker run --rm \
              -v $(location qemu/init.sh):/input/init.sh:ro \
              -v $(location build-scripts/build-initramfs.sh):/build.sh:ro \
              -v $(location :e2fsprogs_deb):/input/e2fsprogs.deb:ro \
              -v $(location :xfsprogs_deb):/input/xfsprogs.deb:ro \
              -v $(location :btrfs_progs_deb):/input/btrfs.deb:ro \
              -v $(@D):/output \
              bazel/vm:build_tools \
              bash /build.sh /input/init.sh /input/e2fsprogs.deb /input/xfsprogs.deb /input/btrfs.deb /output/initramfs.img
      """,
      tags = ["docker"],
  )
  ```

**Benefits**:
- ✅ Genrule is simple and readable
- ✅ Script can be tested independently
- ✅ No cpio, gzip, ar, etc. needed on host
- ✅ All dependencies resolved by apt in container

## Phase 3: QEMU Launcher

### 3.1 Create QEMU Wrapper Script
**File**: `vm/qemu/run-qemu-test.sh`

- [ ] Create script that launches QEMU with correct parameters
  ```bash
  #!/bin/bash
  set -euo pipefail
  
  # Arguments: filesystem duration readers writers
  FILESYSTEM=$1
  DURATION=${2:-300}
  READERS=${3:-10}
  WRITERS=${4:-3}
  
  KERNEL="$RUNFILES_DIR/_main/vm/vmlinuz"
  INITRD="$RUNFILES_DIR/_main/vm/initramfs.img"
  XIBALBA_BIN="$RUNFILES_DIR/_main"  # Has packaging/xibalba_*
  
  # Create test disk
  TEST_DISK=$(mktemp -u).qcow2
  qemu-img create -f qcow2 "$TEST_DISK" 5G
  
  # Launch QEMU
  qemu-system-x86_64 \
    -enable-kvm \
    -cpu host \
    -m 2048 \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -append "console=ttyS0 xibalba.fs=$FILESYSTEM xibalba.duration=$DURATION" \
    -fsdev local,id=xibalba,path="$XIBALBA_BIN",security_model=none,readonly=on \
    -device virtio-9p-pci,fsdev=xibalba,mount_tag=xibalba \
    -drive file="$TEST_DISK",if=virtio,format=qcow2 \
    -nographic \
    -serial stdio
  
  EXIT_CODE=$?
  rm -f "$TEST_DISK"
  exit $EXIT_CODE
  ```

- [ ] Make script executable
- [ ] Handle test disk cleanup properly

### 3.2 Create Bazel sh_binary
**File**: `vm/BUILD.bazel`

- [ ] Add sh_binary for QEMU runner
  ```python
  sh_binary(
      name = "qemu_test_runner",
      srcs = ["qemu/run-qemu-test.sh"],
      data = [
          ":extract_kernel",
          ":custom_initramfs",
          "//packaging:xibalba-deb",  # Or individual binaries
          "//chaos:pause_controller",
          "//chaos:simple_chaos_test",
      ],
  )
  ```

## Phase 4: Test Rules

### 4.1 Create Test Wrapper
**File**: `vm/qemu/test-wrapper.sh`

- [ ] Create wrapper that captures output and determines pass/fail
  ```bash
  #!/bin/bash
  set -euo pipefail
  
  FILESYSTEM=$1
  OUTPUT=$(mktemp)
  
  # Run test and capture output
  if "$RUNFILES_DIR/_main/vm/qemu_test_runner" "$FILESYSTEM" > "$OUTPUT" 2>&1; then
      VM_EXIT=$?
  else
      VM_EXIT=$?
  fi
  
  # Parse output for test results
  if grep -q "XIBALBA_TEST_COMPLETE" "$OUTPUT"; then
      EXIT_CODE=$(grep "EXIT_CODE=" "$OUTPUT" | cut -d= -f2)
      
      # Show test summary
      echo "=== Test Results for $FILESYSTEM ==="
      grep -A 20 "XIBALBA_TEST_COMPLETE" "$OUTPUT"
      
      exit "$EXIT_CODE"
  else
      echo "=== VM failed to complete test ==="
      cat "$OUTPUT"
      exit 1
  fi
  ```

### 4.2 Create Test Macro
**File**: `vm/qemu/qemu_test.bzl`

- [ ] Create Starlark macro for filesystem tests
  ```python
  def qemu_filesystem_test(name, filesystem, **kwargs):
      """Test a filesystem using fast QEMU execution.
      
      Args:
          name: Test name
          filesystem: Filesystem type (ext4, zfs, xfs, etc.)
          **kwargs: Additional test() kwargs
      """
      native.sh_test(
          name = name,
          srcs = ["qemu/test-wrapper.sh"],
          args = [filesystem],
          data = [":qemu_test_runner"],
          tags = ["requires-kvm", "local"],
          size = "large",
          timeout = "moderate",
          **kwargs
      )
  ```

### 4.3 Create Test Suite
**File**: `vm/BUILD.bazel`

- [ ] Use macro to create tests for each filesystem
  ```python
  load(":qemu/qemu_test.bzl", "qemu_filesystem_test")
  
  qemu_filesystem_test(
      name = "qemu_test_ext4",
      filesystem = "ext4",
  )
  
  qemu_filesystem_test(
      name = "qemu_test_zfs",
      filesystem = "zfs",
  )
  
  test_suite(
      name = "qemu_gauntlet_tests",
      tests = [
          ":qemu_test_ext4",
          ":qemu_test_zfs",
      ],
  )
  ```

## Phase 5: Integration and Testing

### 5.1 Basic Validation
- [ ] Build all targets: `bazel build //vm:extract_kernel //vm:custom_initramfs`
- [ ] Test QEMU runner manually: `bazel run //vm:qemu_test_runner -- ext4`
- [ ] Verify 9p mount works
- [ ] Verify test disk creation works
- [ ] Verify serial console output captured

### 5.2 Run Tests
- [ ] Run single test: `bazel test //vm:qemu_test_ext4`
- [ ] Run all tests: `bazel test //vm:qemu_gauntlet_tests`
- [ ] Verify parallel execution works
- [ ] Measure actual boot time

### 5.3 Result Parsing
- [ ] Parse JSON results from serial console
- [ ] Create summary output
- [ ] Integrate with CI

## Open Questions / Decisions Needed

### Q1: Kernel/Initrd Extraction Method ✅ DECIDED
**Chosen**: Download Ubuntu kernel .deb + extract in Docker container

**Why Docker Container**:
- ✅ No host tools needed (ar, tar, xz-utils)
- ✅ Developer only needs Docker + Bazel
- ✅ Reproducible (container + package versions pinned)
- ✅ Works in CI out of the box

See Phase 1.2 for implementation details.

### Q2: Filesystem Tools ✅ DECIDED
**Chosen**: Build minimal initramfs in Docker container

**Why Docker Container**:
- ✅ Apt handles all dependencies automatically
- ✅ dpkg-deb available in container (not needed on host)
- ✅ cpio/gzip available in container (not needed on host)
- ✅ Can install busybox from Ubuntu package

See Phase 2.2 for implementation details.

### Q3: Xibalba Binary Sharing ✅ DECIDED
**Chosen**: Create staging directory with xibalba binaries from our build

**Approach**: Simple genrule to copy binaries (no container needed for this)
```python
genrule(
    name = "xibalba_staging",
    srcs = [
        "//chaos:pause_controller",
        "//chaos:simple_chaos_test", 
        "//vm:xibalba-gauntlet.sh",
    ],
    outs = ["xibalba-staging-dir"],
    cmd = """
        mkdir -p $@/bin
        cp $(location //chaos:pause_controller) $@/bin/
        cp $(location //chaos:simple_chaos_test) $@/bin/
        cp $(location //vm:xibalba-gauntlet.sh) $@/bin/xibalba-gauntlet
        chmod +x $@/bin/*
    """,
)
```

**Benefits**:
- ✅ Hermetic (only our built binaries)
- ✅ Shared via 9p to VM (no installation needed)

### Q4: ZFS Support
**Challenge**: ZFS kernel modules need to be loaded

**Options**:
- A. Use Ubuntu kernel with ZFS modules built-in
- B. Add ZFS modules to initramfs
- C. Skip ZFS in first iteration

**Recommendation**: Try A first, fallback to C if needed

## Success Criteria

- [ ] Boot time < 15 seconds (goal: ~10s)
- [ ] Tests run successfully for ext4
- [ ] Tests run successfully for zfs
- [ ] Parallel execution works (multiple VMs at once)
- [ ] Results captured and parseable
- [ ] No manual steps (fully automated via Bazel)
- [ ] Hermetic (no host dependencies except qemu/kvm)

## Rollout Plan

1. **Proof of Concept** (this plan)
   - Get one test working end-to-end
   - Validate speed improvements
   
2. **Feature Parity**
   - Support all filesystems we currently test
   - Match current test coverage
   
3. **Replace Old System**
   - Mark SSH-based tests as legacy
   - Update documentation
   - Migrate CI

4. **Cleanup**
   - Remove old SSH-based infrastructure
   - Archive old docs with migration notes

## Estimated Timeline

- Phase 1-2 (Foundation + Init): 1 day
- Phase 3 (QEMU Launcher): 0.5 days
- Phase 4 (Test Rules): 0.5 days
- Phase 5 (Integration): 1 day
- **Total**: 3 days for working POC

## Prerequisites

**Truly Hermetic Build** - minimal host requirements!

Developer machine only needs:
- [x] **Docker** (for hermetic build environment)
- [x] **Bazel** (build system)
- [x] **qemu-system-x86_64** (to run VMs)
- [x] **KVM kernel module** (already have: kvm_amd)

**That's it!** No `apt install` anything, no sudo for builds, no host tools!

All build tools run in Docker containers:
- `ar`, `tar`, `cpio`, `gzip` - in build container
- Kernel extraction - in container
- Initramfs creation - in container
- All dependencies resolved by Docker/Bazel

All runtime dependencies downloaded by Bazel:
- Kernel from Ubuntu package archive
- Filesystem tools from Ubuntu packages
- Xibalba binaries from our own build

## Key Innovation: Docker as Build Environment

The breakthrough insight is using **Docker containers as hermetic build tools**:

```
Traditional Approach (❌):
└─ Requires: apt install binutils cpio xz-utils libguestfs-tools...
   └─ Different versions on different machines = non-hermetic

Our Approach (✅):
└─ Requires: Docker
   └─ Build container has exact tool versions
      └─ Reproducible everywhere (dev machine, CI, anywhere)
```

**What runs in containers**:
- Kernel extraction from .deb
- Initramfs creation
- Filesystem tool extraction
- All apt/dpkg operations

**What runs on host**:
- Bazel (orchestration)
- QEMU (running VMs)
- Docker (container runtime)

**Result**: Developers only need Docker + Bazel + QEMU. Everything else is hermetic!

## Next Steps

1. ✅ Review this plan (you're here!)
2. [ ] Get approval to proceed
3. [ ] Verify Docker is installed: `docker --version`
4. [ ] Start implementation at Phase 1.0

