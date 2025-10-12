"""Bazel rules for fast QEMU-based filesystem testing."""

def qemu_filesystem_test(name, filesystem, duration = 60, readers = 10, writers = 3, model = "posix", **kwargs):
    """Create a fast QEMU VM test for a specific filesystem.
    
    This runs xibalba tests in a lightweight QEMU VM that boots in ~1 second.
    No SSH, no network, no cloud-init - just direct kernel boot with embedded binaries.
    
    Args:
        name: Test name
        filesystem: Filesystem type (ext4, xfs, btrfs, zfs, etc.)
        duration: Test duration in seconds (default: 60)
        readers: Number of reader threads (default: 10)
        writers: Number of writer threads (default: 3)
        model: Consistency model (posix, weak, strict, eventual - default: posix)
        **kwargs: Additional sh_test() kwargs (size, timeout, tags, etc.)
    """
    native.sh_test(
        name = name,
        srcs = ["qemu/test-wrapper.sh"],
        args = [
            "--filesystem", filesystem,
            "--duration", str(duration),
            "--readers", str(readers),
            "--writers", str(writers),
            "--model", model,
        ],
        data = [
            ":qemu_test_runner",
            "qemu/run-qemu-test.sh",
            ":extract_kernel",
            ":build_initramfs",
        ],
        tags = kwargs.pop("tags", []) + ["requires-kvm", "local", "no-sandbox"],
        size = kwargs.pop("size", "large"),
        timeout = kwargs.pop("timeout", "moderate"),
        **kwargs
    )

def qemu_filesystem_suite(name, filesystems, **kwargs):
    """Create a test suite for multiple filesystems tested in parallel.
    
    Args:
        name: Test suite name
        filesystems: List of filesystem types to test
        **kwargs: Additional args passed to qemu_filesystem_test
    """
    tests = []
    for fs in filesystems:
        test_name = "qemu_test_{}".format(fs)
        qemu_filesystem_test(
            name = test_name,
            filesystem = fs,
            **kwargs
        )
        tests.append(":{}".format(test_name))
    
    native.test_suite(
        name = name,
        tests = tests,
    )

