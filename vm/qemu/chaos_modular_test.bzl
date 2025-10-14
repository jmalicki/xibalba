"""Bazel rules for testing modular chaos framework in QEMU VMs."""

def chaos_modular_test(
    name,
    workload,
    injector = "none",
    filesystem = "btrfs",
    duration = 60,
    model = "posix",
    **kwargs):
    """Create a QEMU VM test for modular chaos framework.
    
    Tests the new chaos_test_runner with pluggable workloads and injectors.
    
    Args:
        name: Test name
        workload: Workload module (create_delete, rename, hardlink, mixed)
        injector: eBPF injector (none, rename_tracepoint, link_tracepoint, etc.)
        filesystem: Filesystem type (default: btrfs)
        duration: Test duration in seconds (default: 60)
        model: Consistency model (default: posix)
        **kwargs: Additional sh_test() kwargs
    """
    native.sh_test(
        name = name,
        srcs = ["qemu/test-wrapper-modular.sh"],
        args = [
            "--workload", workload,
            "--injector", injector,
            "--filesystem", filesystem,
            "--duration", str(duration),
            "--model", model,
        ],
        data = [
            ":qemu_modular_runner",
            "qemu/run-qemu-modular-test.sh",
            ":extract_kernel",
            ":build_initramfs_modular",
        ],
        tags = kwargs.pop("tags", []) + ["requires-kvm", "local", "no-sandbox", "modular"],
        size = kwargs.pop("size", "large"),
        timeout = kwargs.pop("timeout", "moderate"),
        **kwargs
    )

def chaos_modular_suite(name, workloads, injectors, **kwargs):
    """Create test suite for workload × injector combinations.
    
    Args:
        name: Suite name
        workloads: List of workload names
        injectors: List of injector names
        **kwargs: Additional args for tests
    """
    tests = []
    
    for workload in workloads:
        for injector in injectors:
            test_name = "chaos_{}_{} ".format(workload, injector)
            chaos_modular_test(
                name = test_name,
                workload = workload,
                injector = injector,
                **kwargs
            )
            tests.append(":{}".format(test_name))
    
    native.test_suite(
        name = name,
        tests = tests,
        tags = ["modular", "chaos"],
    )

