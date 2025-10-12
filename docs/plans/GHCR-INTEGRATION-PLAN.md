# GitHub Container Registry (GHCR) Integration Plan

## Goal

Cache the Bazel-built Docker container on GHCR so CI runs can pull pre-built images instead of rebuilding from scratch.

**Expected Benefit:** 2-3 minute CI speedup per run

---

## Architecture

```
┌─────────────────────┐
│  Pull Request / PR  │
│                     │
│  1. Checkout code   │
│  2. Compute cache   │
│     key from locks  │
└──────────┬──────────┘
           │
           ↓
    ┌──────────────┐
    │ Cache exists?│
    └──┬────────┬──┘
       │ NO     │ YES
       ↓        ↓
  ┌────────┐  ┌────────────────┐
  │ Build  │  │ Pull from GHCR │
  │ with   │  │ ghcr.io/...    │
  │ Bazel  │  │ :cache-abc123  │
  └───┬────┘  └────────┬───────┘
      │                │
      ↓                │
  ┌────────────┐       │
  │ Push to    │       │
  │ GHCR with  │       │
  │ cache key  │       │
  └──────┬─────┘       │
         │             │
         └─────┬───────┘
               ↓
       ┌───────────────┐
       │ Use container │
       │ for CI builds │
       └───────────────┘
```

---

## Cache Key Strategy

### Cache Key = Hash of:
1. `MODULE.bazel.lock` (Bazel dependencies)
2. `packages.lock.json` (Debian package versions)

**Example:**
```bash
CACHE_KEY=$(cat MODULE.bazel.lock tools/ci/docker/packages.lock.json | sha256sum | cut -c1-16)
# Output: abc123def4567890
```

**Image Tag:** `ghcr.io/jmalicki/xibalba-dev-env:cache-abc123def4567890`

### Why This Works:
- Lock files only change when dependencies change
- Same locks = identical container (hermetic)
- Cross-branch/PR caching: Same locks = same cache key = reuse image
- Cache invalidation: Lock changes = new key = rebuild

---

## Implementation Steps

### Phase 1: Local Build & Push Script ⏱️ 30 minutes

Create `tools/ci/docker/push_to_ghcr.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

# Generate cache key
CACHE_KEY=$(cat MODULE.bazel.lock tools/ci/docker/packages.lock.json | sha256sum | cut -c1-16)
IMAGE_NAME="ghcr.io/jmalicki/xibalba-dev-env"
IMAGE_TAG="${IMAGE_NAME}:cache-${CACHE_KEY}"

echo "📦 Building container with Bazel..."
bazel build //tools/ci/docker:xibalba_dev_env_tarball

echo "🐳 Loading into Docker..."
docker load < bazel-bin/tools/ci/docker/xibalba_dev_env_tarball/tarball.tar

echo "🏷️  Tagging for GHCR..."
docker tag xibalba-dev:local "${IMAGE_TAG}"
docker tag xibalba-dev:local "${IMAGE_NAME}:latest"

echo "⬆️  Pushing to GHCR..."
echo "${GHCR_TOKEN}" | docker login ghcr.io -u jmalicki --password-stdin
docker push "${IMAGE_TAG}"
docker push "${IMAGE_NAME}:latest"

echo "✅ Pushed: ${IMAGE_TAG}"
```

**Bazel target:**
```starlark
# tools/ci/docker/BUILD.bazel
sh_binary(
    name = "push_to_ghcr",
    srcs = ["push_to_ghcr.sh"],
    data = [
        ":xibalba_dev_env_tarball",
        "//:MODULE.bazel.lock",
        ":packages.lock.json",
    ],
)
```

**Test locally:**
```bash
export GHCR_TOKEN="your_github_pat"
bazel run //tools/ci/docker:push_to_ghcr
```

---

### Phase 2: GitHub Actions Workflow ⏱️ 1 hour

Create `.github/workflows/build-container.yml`:

```yaml
name: Build and Cache Container

on:
  push:
    branches: [main]
    paths:
      - 'MODULE.bazel.lock'
      - 'tools/ci/docker/packages.lock.json'
      - 'tools/ci/docker/**'
  pull_request:
    paths:
      - 'MODULE.bazel.lock'
      - 'tools/ci/docker/packages.lock.json'
      - 'tools/ci/docker/**'

env:
  REGISTRY: ghcr.io
  IMAGE_NAME: ${{ github.repository_owner }}/xibalba-dev-env

permissions:
  contents: read
  packages: write

jobs:
  build-container:
    runs-on: ubuntu-latest
    
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Generate cache key
        id: cache-key
        run: |
          CACHE_KEY=$(cat MODULE.bazel.lock tools/ci/docker/packages.lock.json | sha256sum | cut -c1-16)
          echo "key=${CACHE_KEY}" >> $GITHUB_OUTPUT
          echo "📝 Cache key: ${CACHE_KEY}"

      - name: Check if image exists
        id: check-image
        run: |
          IMAGE="${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:cache-${{ steps.cache-key.outputs.key }}"
          if docker manifest inspect "${IMAGE}" >/dev/null 2>&1; then
            echo "exists=true" >> $GITHUB_OUTPUT
            echo "✅ Image exists: ${IMAGE}"
          else
            echo "exists=false" >> $GITHUB_OUTPUT
            echo "❌ Image not found: ${IMAGE}"
          fi

      - name: Setup Bazel
        if: steps.check-image.outputs.exists == 'false'
        uses: bazel-contrib/setup-bazel@0.8.1
        with:
          bazelisk-version: '1.x'

      - name: Build container with Bazel
        if: steps.check-image.outputs.exists == 'false'
        run: |
          bazel build //tools/ci/docker:xibalba_dev_env_tarball
          docker load < bazel-bin/tools/ci/docker/xibalba_dev_env_tarball/tarball.tar

      - name: Log in to GHCR
        if: steps.check-image.outputs.exists == 'false'
        uses: docker/login-action@v3
        with:
          registry: ${{ env.REGISTRY }}
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}

      - name: Push to GHCR
        if: steps.check-image.outputs.exists == 'false'
        run: |
          IMAGE="${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}"
          CACHE_TAG="${IMAGE}:cache-${{ steps.cache-key.outputs.key }}"
          LATEST_TAG="${IMAGE}:latest"
          
          docker tag xibalba-dev:local "${CACHE_TAG}"
          docker push "${CACHE_TAG}"
          
          if [ "${{ github.ref }}" == "refs/heads/main" ]; then
            docker tag xibalba-dev:local "${LATEST_TAG}"
            docker push "${LATEST_TAG}"
          fi
          
          echo "✅ Pushed: ${CACHE_TAG}"
```

---

### Phase 3: Update Main CI to Use Container ⏱️ 30 minutes

Update `.github/workflows/ci.yml`:

```yaml
name: CI

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    
    # Run inside the pre-built container
    container:
      image: ghcr.io/jmalicki/xibalba-dev-env:cache-${{ steps.cache-key.outputs.key }}
      credentials:
        username: ${{ github.actor }}
        password: ${{ secrets.GITHUB_TOKEN }}
    
    steps:
      - name: Generate cache key
        id: cache-key
        run: |
          CACHE_KEY=$(cat MODULE.bazel.lock tools/ci/docker/packages.lock.json | sha256sum | cut -c1-16)
          echo "key=${CACHE_KEY}" >> $GITHUB_OUTPUT

      - name: Checkout
        uses: actions/checkout@v4

      # No more apt install step! Tools already in container.
      
      - name: Build Xibalba
        run: |
          # All tools available: clang-18, libbpf, headers, etc.
          bazel build //...

      - name: Run tests
        run: |
          bazel test //...
```

**Alternative (if container: doesn't work):**

```yaml
    steps:
      - name: Generate cache key
        id: cache-key
        run: |
          CACHE_KEY=$(cat MODULE.bazel.lock tools/ci/docker/packages.lock.json | sha256sum | cut -c1-16)
          echo "key=${CACHE_KEY}" >> $GITHUB_OUTPUT

      - name: Pull container
        run: |
          IMAGE="ghcr.io/jmalicki/xibalba-dev-env:cache-${{ steps.cache-key.outputs.key }}"
          docker pull "${IMAGE}" || {
            echo "⚠️  Cache miss - building locally"
            bazel build //tools/ci/docker:xibalba_dev_env_tarball
            docker load < bazel-bin/tools/ci/docker/xibalba_dev_env_tarball/tarball.tar
          }

      - name: Build in container
        run: |
          docker run --rm -v $PWD:/workspace -w /workspace \
            ghcr.io/jmalicki/xibalba-dev-env:cache-${{ steps.cache-key.outputs.key }} \
            bash -c "bazel build //... && bazel test //..."
```

---

### Phase 4: GHCR Configuration ⏱️ 15 minutes

#### 4.1 Make Repository Public (or configure access)

**Option A: Public package (recommended for open-source)**
```bash
# In GitHub UI:
# 1. Go to: https://github.com/users/jmalicki/packages/container/xibalba-dev-env/settings
# 2. Scroll to "Danger Zone"
# 3. Click "Change visibility" → "Public"
```

**Option B: Keep private (requires auth in CI)**
```yaml
# PRs from forks won't have access to GITHUB_TOKEN with package:write
# Need to handle this case
```

#### 4.2 Test Manual Push

```bash
# Generate PAT at: https://github.com/settings/tokens
# Scopes needed: write:packages, read:packages, delete:packages

export GHCR_TOKEN="ghp_xxxxxxxxxxxx"
echo "${GHCR_TOKEN}" | docker login ghcr.io -u jmalicki --password-stdin

# Build and push
bazel run //tools/ci/docker:push_to_ghcr
```

#### 4.3 Verify on GHCR

```bash
# Pull to verify
CACHE_KEY=$(cat MODULE.bazel.lock tools/ci/docker/packages.lock.json | sha256sum | cut -c1-16)
docker pull ghcr.io/jmalicki/xibalba-dev-env:cache-${CACHE_KEY}
docker run --rm ghcr.io/jmalicki/xibalba-dev-env:cache-${CACHE_KEY} \
  /usr/bin/bash -c "clang-18 --version"
```

---

### Phase 5: Optimize Cache Strategy ⏱️ 1 hour

#### 5.1 Add Fallback to Latest

If exact cache key not found, try `:latest`:

```yaml
      - name: Pull container
        run: |
          IMAGE="ghcr.io/jmalicki/xibalba-dev-env"
          CACHE_TAG="${IMAGE}:cache-${{ steps.cache-key.outputs.key }}"
          
          # Try exact cache key
          if docker pull "${CACHE_TAG}"; then
            echo "✅ Cache HIT: ${CACHE_TAG}"
          # Fallback to latest
          elif docker pull "${IMAGE}:latest"; then
            echo "⚠️  Cache MISS, using latest"
            docker tag "${IMAGE}:latest" "${CACHE_TAG}"
          # Build from scratch
          else
            echo "❌ No cache, building..."
            bazel build //tools/ci/docker:xibalba_dev_env_tarball
            docker load < bazel-bin/tools/ci/docker/xibalba_dev_env_tarball/tarball.tar
            docker tag xibalba-dev:local "${CACHE_TAG}"
          fi
```

#### 5.2 Clean Up Old Images

Create `.github/workflows/cleanup-ghcr.yml`:

```yaml
name: Cleanup Old Container Images

on:
  schedule:
    # Run weekly on Sunday at 2 AM UTC
    - cron: '0 2 * * 0'
  workflow_dispatch:

jobs:
  cleanup:
    runs-on: ubuntu-latest
    permissions:
      packages: write
    
    steps:
      - name: Delete old images
        uses: actions/delete-package-versions@v4
        with:
          package-name: xibalba-dev-env
          package-type: container
          min-versions-to-keep: 10
          delete-only-untagged-versions: false
```

---

## Testing Checklist

### Local Testing:
- [ ] `bazel build //tools/ci/docker:xibalba_dev_env_tarball` works
- [ ] Container runs: `docker run --rm xibalba-dev:local /usr/bin/bash -c "clang-18 --version"`
- [ ] Cache key script generates consistent hashes
- [ ] Manual push to GHCR succeeds
- [ ] Manual pull from GHCR succeeds

### CI Testing:
- [ ] Container build workflow triggers on lock file changes
- [ ] Container build workflow skips if image exists
- [ ] Main CI pulls container successfully
- [ ] Main CI builds Xibalba in container
- [ ] CI speedup measured (before/after comparison)

---

## Expected Results

### Before (Current):
```
CI Run Time Breakdown:
- Checkout: 10s
- Setup dependencies (apt): 180s  ← SLOW
- Build Xibalba: 60s
- Run tests: 30s
Total: ~280s (4.5 minutes)
```

### After (With GHCR):
```
CI Run Time Breakdown:
- Checkout: 10s
- Pull container (cache hit): 10s  ← FAST!
- Build Xibalba: 60s
- Run tests: 30s
Total: ~110s (2 minutes)

Speedup: 170 seconds saved (60% faster)
```

### Cache Scenarios:

**Scenario 1: Exact cache hit (most common)**
- Same lock files → Same cache key → Pull cached image (10s)

**Scenario 2: Cache miss, but :latest exists**
- Different locks but close → Pull :latest, rebuild layers (30s)

**Scenario 3: Complete miss (rare)**
- First build ever or major upgrade → Build with Bazel (5-7s local, 60s CI)

---

## Rollout Plan

### Week 1: Local Validation
1. ✅ Create push script
2. ✅ Test manual GHCR push
3. ✅ Verify pull works
4. ✅ Test cache key generation

### Week 2: CI Integration
1. Add build-container.yml workflow
2. Test on feature branch
3. Merge to main
4. Monitor first builds

### Week 3: Update Main CI
1. Update ci.yml to use container
2. Test PR builds
3. Measure speedup
4. Document results

### Week 4: Optimize
1. Add cleanup workflow
2. Tune cache strategy
3. Monitor cache hit rates
4. Iterate based on data

---

## Monitoring & Metrics

### Track These Metrics:

**Cache Hit Rate:**
```bash
# How often we reuse cached images
Cache Hits / Total Builds * 100
Target: > 80%
```

**CI Time Savings:**
```bash
# Average time saved per build
(Old CI Time - New CI Time) * Builds Per Day
Expected: 170s * 50 builds = 2.4 hours/day saved
```

**Storage Costs:**
```bash
# GHCR storage (free for public repos)
Image Size: ~400MB
Keep 10 versions: ~4GB
Cost: $0 (public) or ~$0.40/month (private)
```

---

## Troubleshooting

### Issue: "Image not found" in CI
**Solution:** Check GITHUB_TOKEN permissions include `packages: write`

### Issue: "Cannot pull - authentication required"
**Solution:** Make package public or add auth to docker pull step

### Issue: Cache never hits
**Solution:** Verify cache key generation is deterministic

### Issue: Image too large
**Solution:** Review packages.yaml, remove unnecessary packages

---

## Security Considerations

1. **Public vs Private Images:**
   - Public: Anyone can pull (good for open-source)
   - Private: Only authenticated users (better for proprietary code)

2. **Image Scanning:**
   ```yaml
   - name: Scan image
     uses: aquasecurity/trivy-action@master
     with:
       image-ref: ghcr.io/jmalicki/xibalba-dev-env:cache-${{ steps.cache-key.outputs.key }}
   ```

3. **Supply Chain:**
   - Lock files prevent supply chain attacks
   - Snapshot repository ensures packages don't change
   - Bazel verifies checksums

---

## Cost Analysis

### GitHub Container Registry (GHCR):

**Free Tier (Public repositories):**
- Unlimited public container storage
- Unlimited bandwidth for public images
- **Cost: $0/month**

**Private repositories:**
- 500MB free storage
- Each additional GB: $0.25/month
- Our usage: ~4GB (10 versions × 400MB)
- **Cost: ~$0.88/month**

**Time Savings Value:**
- 170s saved per build
- 50 builds/day average
- 2.4 hours/day saved
- At $100/hour CI cost: **$240/day saved**

**ROI: 273,000% 🚀**

---

## Next Steps Summary

### Immediate (Today):
1. ✅ Create `tools/ci/docker/push_to_ghcr.sh`
2. ✅ Test local push to GHCR
3. ✅ Make package public

### Short Term (This Week):
4. ⏭️ Create `.github/workflows/build-container.yml`
5. ⏭️ Test workflow on feature branch
6. ⏭️ Update main CI to use container

### Medium Term (Next Week):
7. ⏭️ Monitor cache hit rates
8. ⏭️ Add cleanup workflow
9. ⏭️ Document speedup metrics

---

**Status:** Ready to implement! All prerequisites complete.
**Estimated Total Time:** 3-4 hours
**Expected Benefit:** 60% CI speedup, hermetic builds

