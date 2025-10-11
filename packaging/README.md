# Xibalba Packaging

Debian package creation for Xibalba using Bazel's `rules_pkg`.

## Building the Package

```bash
# Build the .deb package
bazel build //packaging:xibalba-deb

# Output location
bazel-bin/packaging/xibalba_0.1.0_amd64.deb
```

## Package Contents

The Debian package includes:

- `/usr/bin/pause_controller` - eBPF userspace controller
- `/usr/bin/simple_chaos_test` - Multi-threaded directory reading test
- `/usr/bin/pause_injector.bpf.o` - eBPF bytecode

## Dependencies

### Runtime Dependencies (in the .deb)

The package depends on:
- `libbpf0` - eBPF library (runtime)
- `libelf1` - ELF file handling (runtime)

Defined in `packaging/BUILD.bazel` → `pkg_deb.depends`

### Build Dependencies (for compilation)

To **build** Xibalba from source, you need:
- `llvm` - For llvm-objdump (eBPF verification)
- `libbpf-dev` - Development files for libbpf
- `libelf-dev` - Development files for libelf  
- `linux-headers-generic` - Kernel headers for eBPF compilation

Defined in `packaging/BUILD.bazel` → `BUILD_DEPS` variable

### Generating Build Dependencies List

Bazel is the **single source of truth** for dependencies:

```bash
# Generate build-deps.txt
bazel build //packaging:generate_build_deps
cat bazel-bin/packaging/build-deps.txt
```

CI automatically uses this generated file to install build dependencies.

## Installation

### On Ubuntu/Debian:

```bash
# Install the package
sudo dpkg -i xibalba_0.1.0_amd64.deb

# If dependencies are missing, fix them
sudo apt-get install -f
```

### In VMs:

```bash
# Copy to VM
scp xibalba_0.1.0_amd64.deb root@vm:/tmp/

# Install in VM
ssh root@vm "dpkg -i /tmp/xibalba_0.1.0_amd64.deb && apt-get install -f -y"
```

## Package Metadata

- **Package Name**: xibalba
- **Version**: 0.1.0
- **Architecture**: amd64
- **Section**: contrib/devel
- **Priority**: optional
- **Maintainer**: Xibalba Developers

## Inspect Package

```bash
# Show package info
dpkg-deb --info bazel-bin/packaging/xibalba_0.1.0_amd64.deb

# List files in package
dpkg-deb --contents bazel-bin/packaging/xibalba_0.1.0_amd64.deb

# Extract control files
dpkg-deb --control bazel-bin/packaging/xibalba_0.1.0_amd64.deb /tmp/control/
```

## Uninstall

```bash
sudo dpkg -r xibalba
```

## CI Integration

The CI automatically builds and uploads the .deb package as an artifact:

1. **Build stage**: Creates `xibalba_0.1.0_amd64.deb`
2. **Upload**: Stores as GitHub Actions artifact
3. **VM tests**: Downloads and installs in test VMs

No compilation needed in VMs - just `dpkg -i`!

