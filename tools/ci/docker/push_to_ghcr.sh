#!/usr/bin/env bash
set -euo pipefail

# Script to build and push CI container to GitHub Container Registry
# Usage: 
#   export GHCR_TOKEN="ghp_..."
#   bazel run //tools/ci/docker:push_to_ghcr

# Determine if we're running from Bazel or directly
if [ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]; then
    # Running via 'bazel run' - use workspace directory
    REPO_ROOT="${BUILD_WORKSPACE_DIRECTORY}"
else
    # Running directly - find repo root
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
fi

# Navigate to repo root
cd "${REPO_ROOT}"

echo "📝 Generating cache key..."
CACHE_KEY=$(cat MODULE.bazel.lock tools/ci/docker/packages.lock.json | sha256sum | cut -c1-16)
echo "   Cache key: ${CACHE_KEY}"

IMAGE_NAME="ghcr.io/jmalicki/xibalba-dev-env"
CACHE_TAG="${IMAGE_NAME}:cache-${CACHE_KEY}"
LATEST_TAG="${IMAGE_NAME}:latest"

echo ""
echo "📦 Building container with Bazel..."
bazel build //tools/ci/docker:xibalba_dev_env_tarball

echo ""
echo "🐳 Loading into Docker..."
docker load < bazel-bin/tools/ci/docker/xibalba_dev_env_tarball/tarball.tar

echo ""
echo "🏷️  Tagging for GHCR..."
docker tag xibalba-dev:local "${CACHE_TAG}"
docker tag xibalba-dev:local "${LATEST_TAG}"

echo ""
echo "🔐 Logging in to GHCR..."
if [ -z "${GHCR_TOKEN:-}" ]; then
    echo "❌ ERROR: GHCR_TOKEN environment variable not set"
    echo "   Generate a token at: https://github.com/settings/tokens"
    echo "   Scopes needed: write:packages, read:packages"
    echo ""
    echo "   Then run:"
    echo "   export GHCR_TOKEN='ghp_...'"
    echo "   bazel run //tools/ci/docker:push_to_ghcr"
    exit 1
fi

echo "${GHCR_TOKEN}" | docker login ghcr.io -u jmalicki --password-stdin

echo ""
echo "⬆️  Pushing to GHCR..."
echo "   Pushing: ${CACHE_TAG}"
docker push "${CACHE_TAG}"

echo "   Pushing: ${LATEST_TAG}"
docker push "${LATEST_TAG}"

echo ""
echo "✅ SUCCESS! Container pushed to GHCR"
echo ""
echo "📦 Image tags:"
echo "   ${CACHE_TAG}"
echo "   ${LATEST_TAG}"
echo ""
echo "🚀 To use in CI:"
echo "   docker pull ${CACHE_TAG}"
echo "   docker run --rm ${CACHE_TAG} /usr/bin/bash -c 'clang-18 --version'"

