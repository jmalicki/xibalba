# CI Infrastructure

## Overview

This directory contains CI/CD infrastructure for Xibalba.

## Subdirectories

### `docker/` - Container Images for CI

Hermetic dev environment using `rules_distroless` + `rules_oci`.

**Purpose:** Cache CI dependencies to avoid `apt-get install` on every run.

**Benefits:**
- ⚡ 2-3 minutes faster CI
- 🔒 Hermetic (versions in lock file)
- 🔄 Cross-branch caching
- 📦 Bazel-native

**Implementation:** See `/docs/plans/RULES-DISTROLESS-IMPLEMENTATION.md`

---

*For detailed implementation plan, see: `../../docs/plans/RULES-DISTROLESS-IMPLEMENTATION.md`*

