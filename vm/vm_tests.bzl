"""Bazel macros for VM testing.

This eliminates complex shell orchestration by letting Bazel handle:
- Parallel execution (native test parallelism)
- Dependency management (test depends on package)
- Result aggregation (test_suite)
- Artifact collection (test outputs)
"""

def vm_gauntlet_test(name, filesystem, **kwargs):
    """Create a single VM gauntlet test (Bazel-native modular design).
    
    New architecture:
    - Separate scripts for each step (create, wait, deploy, test, cleanup)
    - Clear error codes (10=create, 20=ssh, 30=deploy, 40=test)
    - Each component independently testable
    - Better debugging (know exactly which step failed)
    
    Bazel benefits:
    - Parallel execution across filesystems
    - Standard test output format
    - Automatic artifact collection
    
    Args:
        name: Test name (e.g., "ext4_gauntlet") - used as unique VM name
        filesystem: Filesystem type ("ext4", "zfs", etc.)
        **kwargs: Additional sh_test arguments
    """
    native.sh_test(
        name = name,
        srcs = ["vm_test_orchestrator.sh"],
        args = [
            "--vm-name=" + name,
            "--filesystem=" + filesystem,
            "--package=$(location //packaging:xibalba-deb)",
        ],
        data = [
            # Modular, testable components
            ":create_test_vm.sh",
            ":wait_for_vm.sh",
            ":deploy_xibalba.sh",
            ":run_gauntlet_tests.sh",
            ":destroy_vm.sh",
            # Dependencies
            ":verify_host_deps.sh",
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

