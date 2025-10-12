# Research: Existing Bazel Rules for APT Packages

## Question
Are there publicly available Bazel rules for `apt-get` integration that we can use instead of building our own?

## Answer: YES! Multiple Options Available

---

## Option 1: rules_distroless ⭐ (RECOMMENDED)

**Status:** Maintained by Google Container Tools  
**Maturity:** Production-ready  
**Approach:** Fetch debian packages hermetically  

### What It Does:
- Downloads `.deb` packages from Debian/Ubuntu repositories
- Creates hermetic OCI image layers
- Integrates with `rules_oci`
- Provides `apt` rules similar to `apt-get install`

### Example Usage:
```python
# MODULE.bazel
bazel_dep(name = "rules_distroless", version = "0.3.7")

# BUILD.bazel
load("@rules_distroless//apt:defs.bzl", "dpkg_status", "dpkg_layer")
load("@rules_oci//oci:defs.bzl", "oci_image")

# Define packages to install
dpkg_layer(
    name = "build_tools",
    dpkgs = [
        "@packages_amd64//clang-20",
        "@packages_amd64//libbpf-dev",
        "@packages_amd64//libelf-dev", 
        "@packages_amd64//linux-headers-generic",
    ],
)

# Create OCI image with packages
oci_image(
    name = "dev_env",
    base = "@ubuntu_base",
    tars = [":build_tools"],
)
```

### Pros:
- ✅ Official Google project
- ✅ Works with rules_oci
- ✅ Hermetic (downloads .deb, pins versions)
- ✅ Bazel-native caching
- ✅ No Docker daemon needed
- ✅ Creates minimal images

### Cons:
- ⚠️ Needs package repository setup
- ⚠️ More complex initial configuration
- ⚠️ Learning curve

### Links:
- GitHub: Search for "GoogleContainerTools/rules_distroless"
- Part of the distroless image ecosystem
- Designed for production use

---

## Option 2: rules_deb_packages

**Status:** Community project  
**Maturity:** Experimental/Medium  
**Approach:** Hermetic debian package management  

### What It Does:
- Manages `.deb` packages in Bazel
- Download and extract packages
- Create hermetic builds

### Example:
```python
load("@rules_deb_packages//deb_packages:defs.bzl", "deb_packages")

deb_packages(
    name = "dev_packages",
    packages = {
        "libbpf-dev": "1:1.2.0-1",
        "libelf-dev": "0.188-2.1",
    },
    sources = ["http://archive.ubuntu.com/ubuntu"],
)
```

### Pros:
- ✅ Simpler than rules_distroless
- ✅ Direct package management
- ✅ Version pinning

### Cons:
- ⚠️ Less maintained
- ⚠️ May not integrate well with rules_oci
- ⚠️ Smaller community

### Links:
- GitHub: petermylemans/rules_deb_packages
- Community project

---

## Option 3: Custom genrule (What We'd Build)

**Status:** DIY  
**Maturity:** N/A (we'd build it)  
**Approach:** Shell script in genrule  

### What We'd Do:
```python
genrule(
    name = "apt_layer",
    outs = ["apt_layer.tar"],
    cmd = """
        # Use apt-get to download packages
        apt-get update
        apt-get download clang-20 libbpf-dev libelf-dev
        # Extract and tar them
        mkdir -p rootfs
        dpkg -x *.deb rootfs/
        tar -czf $@ -C rootfs .
    """,
)
```

### Pros:
- ✅ Full control
- ✅ Simple to understand
- ✅ No external dependencies

### Cons:
- ❌ Not hermetic (downloads at build time)
- ❌ No version pinning
- ❌ Not reproducible
- ❌ Violates Bazel principles
- ❌ We maintain it

---

## Option 4: Dockerfile + Docker Build (Simplest)

**Status:** Standard Docker  
**Maturity:** Very mature  
**Approach:** Traditional Dockerfile  

### What We'd Do:
```dockerfile
# tools/ci/docker/Dockerfile
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    clang-20 \
    libbpf-dev \
    libelf-dev \
  && apt-get clean
```

### Pros:
- ✅ Simplest approach
- ✅ Well-documented
- ✅ Works TODAY
- ✅ GitHub Actions supports Docker caching

### Cons:
- ❌ Not Bazel-managed
- ❌ Not hermetic (builds vary over time)
- ❌ Need Docker daemon
- ❌ Separate from Bazel

---

## Recommendation

### For Immediate Implementation: Dockerfile (Option 4)

**Why:**
1. **Works in 1-2 hours** (vs. days for rules_distroless)
2. **No new Bazel rules to learn**
3. **GitHub Actions has native Docker support**
4. **Can migrate to rules_distroless later**

### For Long-Term: rules_distroless (Option 1)

**Why:**
1. **Hermetic** - Bazel-managed dependencies
2. **Official Google project** - Production-ready
3. **Integrates with rules_oci** - Clean architecture
4. **Reproducible** - Pinned package versions

### Migration Path:

```
Phase 1: Dockerfile (Quick win - 2 hours)
   ↓
Phase 2: Test in production (1 week)
   ↓
Phase 3: Migrate to rules_distroless (3-5 days)
   ↓
Phase 4: Fully hermetic CI (DONE!)
```

---

## Detailed Comparison

| Feature | Dockerfile | rules_distroless | Custom genrule | rules_deb_packages |
|---------|-----------|------------------|----------------|-------------------|
| **Setup Time** | 1-2 hours | 1-2 days | 3-4 hours | 1 day |
| **Hermetic** | ❌ No | ✅ Yes | ❌ No | ✅ Yes |
| **Bazel-native** | ❌ No | ✅ Yes | ⚠️ Partial | ✅ Yes |
| **Reproducible** | ⚠️ Tags only | ✅ Yes | ❌ No | ✅ Yes |
| **Maintained** | ✅ Docker | ✅ Google | ❌ Us | ⚠️ Community |
| **Documentation** | ✅ Excellent | ⚠️ Medium | ❌ None | ⚠️ Limited |
| **Learning Curve** | ✅ Easy | ⚠️ Medium | ✅ Easy | ⚠️ Medium |
| **Works with rules_oci** | ❌ Separate | ✅ Yes | ⚠️ Hacky | ⚠️ Maybe |
| **GitHub Actions** | ✅ Native | ⚠️ Custom | ⚠️ Custom | ⚠️ Custom |

---

## Updated Implementation Plan

### Phase 1: Quick Win with Dockerfile (2 hours) ✅ START HERE

**Directory:** `tools/ci/docker/`

**Files to Create:**
1. `tools/ci/docker/Dockerfile` - Standard Dockerfile
2. `tools/ci/docker/generate_cache_key.sh` - Hash generator
3. `.github/workflows/build-dev-image.yml` - Build workflow
4. Update `.github/workflows/ci.yml` - Use container

**No custom Bazel rules needed!** Use Docker's built-in caching.

### Phase 2: Migrate to rules_distroless (Later, 3-5 days)

**When:** After Dockerfile proves the concept works

**What to Add:**
1. `bazel_dep(name = "rules_distroless", version = "0.3.7")` to MODULE.bazel
2. Define apt package sources
3. Create `dpkg_layer` targets
4. Integrate with existing `oci_image`

**Benefits:**
- Fully hermetic
- Bazel manages everything
- Better caching
- More reproducible

---

## Concrete Next Steps

### Use Dockerfile First (Pragmatic Approach):

- [x] Research existing rules (THIS DOCUMENT)
- [ ] Create `tools/ci/docker/Dockerfile`
- [ ] Create `tools/ci/docker/generate_cache_key.sh`
- [ ] Create `.github/workflows/build-dev-image.yml`
- [ ] Update `.github/workflows/ci.yml`
- [ ] Test locally
- [ ] Test in CI
- [ ] Measure time savings
- [ ] Document

**Estimated Time:** 2-3 hours  
**Risk:** Low  
**Complexity:** Low  

### Migrate to rules_distroless Later (Clean Approach):

**When to migrate:**
- After Dockerfile works for 1-2 weeks
- When team has bandwidth
- When hermetic builds are priority

**Estimated Time:** 3-5 days  
**Risk:** Medium  
**Complexity:** Medium  

---

## Example: rules_distroless Integration (Future)

### MODULE.bazel:
```python
bazel_dep(name = "rules_distroless", version = "0.3.7")

# Register Ubuntu package sources
apt = use_extension("@rules_distroless//apt:extensions.bzl", "apt")
apt.install(
    name = "ubuntu_packages",
    lock = "//tools/ci/docker:packages.lock.json",
    manifest = "//tools/ci/docker:packages.yaml",
)
use_repo(apt, "ubuntu_packages")
```

### tools/ci/docker/packages.yaml:
```yaml
version: 1

sources:
  - channel: ubuntu noble amd64
    url: http://archive.ubuntu.com/ubuntu

archs:
  - amd64

packages:
  - clang-20
  - libbpf-dev
  - libelf-dev
  - linux-headers-generic
  - e2fsprogs
  - xfsprogs
  - btrfs-progs
```

### tools/ci/docker/BUILD.bazel:
```python
load("@rules_distroless//apt:defs.bzl", "dpkg_layer")
load("@rules_oci//oci:defs.bzl", "oci_image")

dpkg_layer(
    name = "dev_packages",
    dpkgs = [
        "@ubuntu_packages//clang-20",
        "@ubuntu_packages//libbpf-dev",
        "@ubuntu_packages//libelf-dev",
        "@ubuntu_packages//linux-headers-generic",
    ],
)

oci_image(
    name = "xibalba_dev_env",
    base = "@ubuntu_base",
    tars = [":dev_packages"],
)
```

**Benefits of this approach:**
- ✅ Hermetic (packages pinned in lock file)
- ✅ Bazel-native (integrated caching)
- ✅ Reproducible (lock file version control)
- ✅ Works with rules_oci

---

## Decision Tree

```
Do you need hermetic builds TODAY?
│
├─ NO → Use Dockerfile
│       ↓
│       Fast (2 hours)
│       Works immediately
│       Migrate later
│
└─ YES → Do you have 3-5 days?
         │
         ├─ NO → Use Dockerfile anyway
         │       Then migrate
         │
         └─ YES → Use rules_distroless
                  Hermetic from day 1
                  More work upfront
```

**For most teams:** Start with Dockerfile, migrate to rules_distroless if needed.

---

## Conclusion

**Available Public Rules:**
1. ✅ **rules_distroless** - Best for hermetic builds
2. ✅ **rules_deb_packages** - Alternative
3. ✅ **rules_docker** (deprecated) - Old approach

**Recommendation:**
- **Short term:** Use Dockerfile (2 hours)
- **Long term:** Migrate to rules_distroless (3-5 days)

**You DON'T need to build your own apt integration!**  
Google already built `rules_distroless` for exactly this use case.

---

*Research completed: October 12, 2025*
*For Docker caching strategy investigation*

