"""Bazel macros for VM testing.

This eliminates complex shell orchestration by letting Bazel handle:
- Parallel execution (native test parallelism)
- Dependency management (test depends on package)
- Result aggregation (test_suite)
- Artifact collection (test outputs)
"""

def vm_gauntlet_test(name, filesystem, **kwargs):
    """Create a single VM gauntlet test.
    
    Bazel will:
    - Run tests in parallel automatically
    - Collect outputs to bazel-testlogs/
    - Handle retries and caching
    - Show results in standard format
    
    Each test gets a unique VM name to avoid conflicts.
    
    Args:
        name: Test name (e.g., "ext4_gauntlet")
        filesystem: Filesystem type ("ext4", "zfs", etc.)
        **kwargs: Additional sh_test arguments
    """
    native.sh_test(
        name = name,
        srcs = ["run_single_vm_gauntlet.sh"],
        args = [
            "--filesystem=" + filesystem,
            "--vm-name=" + name,  # Use test name (unique)
            "$(location //packaging:xibalba-deb)",
        ],
        data = [
            ":verify_host_deps.sh",
            ":create_test_vm.sh",
            ":deploy_xibalba.sh",
            ":destroy_vm.sh",
            ":xibalba-gauntlet.sh",
            "//packaging:xibalba-deb",
        ],
        tags = ["manual", "local", "requires-kvm"],
        size = "large",
        timeout = "long",
        **kwargs
    )

def vm_gauntlet_suite(name, filesystems):
    """Create a test suite for multiple filesystems.
    
    Bazel runs these in parallel automatically!
    No need for bash job management.
    
    Args:
        name: Suite name
        filesystems: List of filesystem types to test
    """
    test_targets = []
    
    for fs in filesystems:
        test_name = fs + "_gauntlet"
        vm_gauntlet_test(
            name = test_name,
            filesystem = fs,
        )
        test_targets.append(":" + test_name)
    
    native.test_suite(
        name = name,
        tests = test_targets,
        tags = ["manual", "local"],
    )

