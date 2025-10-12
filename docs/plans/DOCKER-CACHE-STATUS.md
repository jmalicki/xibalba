# Docker Cache Implementation - Current Status

## Summary

✅ **Partially Working** - rules_distroless integrated, images build, but runtime issues

## What Works

- ✅ Added `gazelle` and `rules_distroless` dependencies to MODULE.bazel
- ✅ Created `tools/ci/docker/packages.yaml` manifest
- ✅ Generated `packages.lock.json` (120KB, 3853 lines, pins 700+ packages)
- ✅ Built OCI image with rules_oci + rules_distroless
- ✅ Cache key generation working (`a524adad-05c523af`)
- ✅ Bazel builds complete successfully
- ✅ Image loads into Docker

## What Doesn't Work Yet

- ❌ **Container won't run** - Binary execution fails
- ❌ Bash exists at `/usr/bin/bash` but can't execute
- ❌ Error: "cannot execute binary file"
- ❌ Missing shared library dependencies (libc, etc.)

## Root Cause

**Issue:** rules_distroless packages include binaries but not their shared library dependencies by default.

**Example:** bash binary is installed, but libc6, libncurses, etc. are not properly linked.

## Next Steps to Fix

### Option 1: Add Base System (Recommended)

Need to include fundamental system libraries:

```yaml
packages:
  # Base system
  - libc6
  - libgcc-s1
  - libstdc++6
  - libtinfo6
  - libncurses6
  
  # Then existing packages...
```

### Option 2: Use dpkg_status

rules_distroless has `dpkg_status` which might handle dependencies better:

```python
load("@rules_distroless//apt:defs.bzl", "dpkg_status")

dpkg_status(
    name = "status",
    packages = [...],
)
```

### Option 3: Switch to Debian Distroless Base

Use Google's pre-built distroless base that has libc:

```python
oci.pull(
    name = "distroless_base",
    image = "gcr.io/distroless/base-debian12",
)
```

Then layer our packages on top.

### Option 4: Use Full Ubuntu Base (Simplest)

Just use the ubuntu:24.04 image as-is and add tools via Dockerfile in GitHub Actions.

## Time Investment

**So far:** ~1 hour (good learning, dependencies configured)

**To complete rules_distroless:** +3-5 hours (debug dependency issues)

**Alternative (Dockerfile):** +1 hour (works immediately)

## Recommendation

Given the complexity, I recommend:

1. **Short term:** Use Dockerfile approach for immediate CI speedup
2. **Long term:** Revisit rules_distroless when we need full hermeticity

## Files Created

```
MODULE.bazel - Added gazelle + rules_distroless
tools/ci/docker/
├── BUILD.bazel - OCI image targets
├── packages.yaml - Package manifest
├── packages.lock.json - Generated lock file (120KB)
├── generate_cache_key.sh - Cache key generation
├── README.md - Documentation
└── Dockerfile - For fallback approach
```

## Learnings

1. **rules_distroless works** but requires careful dependency management
2. **Lock file generation works** - `bazel run @ubuntu_packages//:lock`  
3. **OCI image building works** - rules_oci integration successful
4. **Missing system libraries** is the blocker

## Current State of Branch

Branch: `investigate/docker-cache-strategy`

Changes:
- MODULE.bazel (+gazelle, +rules_distroless, +apt extension)
- tools/ci/docker/* (new directory with all files)
- BUILD.bazel (export MODULE.bazel.lock)
- docs/plans/* (implementation plans)

## Decision Point

**Question:** Continue debugging rules_distroless dependencies, or pivot to Dockerfile?

**My recommendation:** Document this investigation, implement Dockerfile for quick win, return to rules_distroless later.

---

*Status as of: October 12, 2025*
*Branch: investigate/docker-cache-strategy*

