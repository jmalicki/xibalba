# VM Base Images

Bazel-managed VM base images for Xibalba testing.

## Concept

**Build once, use everywhere**: Create a base VM image with all dependencies pre-installed, then create lightweight clones for each filesystem test.

### Advantages

✅ **Build VM deps once** (not 4 times)  
✅ **Fast cloning** (copy-on-write qcow2)  
✅ **Hermetic** (Bazel-managed)  
✅ **Cacheable** (Bazel remote cache)  
✅ **Reproducible** (same base image everywhere)  

---

## How It Works

### 1. Base Image Creation

```bash
# Build base VM image (once, ~5 minutes)
bazel build //vm/images:base_vm_image
```

**What this does**:
- Downloads Ubuntu 24.04 cloud image
- Uses `virt-customize` to install:
  - QEMU guest agent
  - Linux kernel headers
  - libbpf-dev, libbpf0, libelf1
- Configures SSH access
- Creates: `bazel-bin/vm/images/xibalba-base.qcow2`

### 2. Clone for Each Filesystem

```bash
# Create filesystem-specific VMs (fast, copy-on-write)
bazel build //vm/images:ext4_vm_image
bazel build //vm/images:xfs_vm_image
bazel build //vm/images:btrfs_vm_image
bazel build //vm/images:tmpfs_vm_image

# Or all at once
bazel build //vm/images:all_vm_images
```

**What this does**:
- Creates copy-on-write qcow2 images
- Each backed by base image
- Minimal disk usage (deltas only)
- Instant creation (~1 second each)

---

## CI Integration

### Current CI (Inefficient)

```yaml
vm-tests (matrix):
  - Install VM deps  # 4x (once per job)
  - Create VM        # 4x
  - Run tests        # 4x
```

**Problem**: Installing VM deps 4 times (5+ minutes each)

### Improved CI (Efficient)

```yaml
prepare-vms:
  - Build base VM image (once, 5 minutes)
  - Upload as artifact

vm-tests (matrix):
  - Download base image  # Fast
  - Clone for filesystem # Fast (~1s)
  - Run tests           # Parallel
```

**Benefits**: 
- Build deps once (5 min)
- Clone 4x in parallel (4 seconds total)
- Save ~15 minutes per CI run

---

## Future Implementation

### Phase 1: Local Bazel Build (Current)

```bash
bazel build //vm/images:base_vm_image
# Requires: virt-customize installed locally
```

### Phase 2: CI Integration

```yaml
- name: Build base VM image
  run: |
    sudo apt-get install -y libguestfs-tools
    bazel build //vm/images:base_vm_image

- name: Upload base image
  uses: actions/upload-artifact@v4
  with:
    name: base-vm-image
    path: bazel-bin/vm/images/xibalba-base.qcow2
```

### Phase 3: Bazel Remote Cache

```bash
# Build once on any machine
bazel build //vm/images:base_vm_image --remote_cache=...

# All CI jobs fetch from cache (instant)
bazel build //vm/images:base_vm_image  # <- cached!
```

---

## Manual VM Creation

Without Bazel, using current scripts:

```bash
# Create VM from scratch (current approach)
./vm/create_test_vm.sh --name test-01

# With Bazel-built base image (future)
qemu-img create -f qcow2 -F qcow2 \
  -b bazel-bin/vm/images/xibalba-base.qcow2 \
  test-01.qcow2
```

---

## Dependencies

**Required tools**:
- `virt-customize` (from libguestfs-tools)
- `qemu-img`
- `wget`

**Install**:
```bash
sudo apt-get install -y libguestfs-tools qemu-utils
```

---

## Notes

- Base image is ~2GB (compressed cloud image)
- Cloned images are initially tiny (~200MB for metadata)
- Images grow as tests write data
- All images share base blocks (copy-on-write)

**Storage efficiency**:
- Base: 2 GB
- 4 clones: 4 × 200 MB = 800 MB
- Total: ~2.8 GB (vs 8 GB for 4 full images)

---

**TODO**: Implement this in CI for true "build once, test 4x" workflow!

