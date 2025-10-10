workspace(name = "rudra")

# RUDRA: Chaos Testing Framework for Kernel Filesystems
# Build system: Bazel

# External dependencies
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# Rules for C/C++
# (Built-in to Bazel)

# Rules for shell scripts
http_archive(
    name = "bazel_skylib",
    urls = [
        "https://github.com/bazelbuild/bazel-skylib/releases/download/1.5.0/bazel-skylib-1.5.0.tar.gz",
    ],
    sha256 = "cd55a062e763b9349921f0f5db8c3933288dc8ba4f76dd9416aac68acee3cb94",
)

load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")
bazel_skylib_workspace()

# System libraries (liburing, libbpf)
# These must be installed on the system
# Bazel will link against them

# For eBPF compilation, we'll use clang directly with custom rules



