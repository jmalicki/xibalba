# RUDRA Bazel Workflow

*Everything through Bazel - no ad-hoc scripts*

---

## Philosophy

**RUDRA uses Bazel for all operations** - building, testing, and VM management. This ensures:
- ✅ Hermetic builds (reproducible everywhere)
- ✅ Explicit dependencies (no hidden requirements)
- ✅ Incremental compilation (only rebuild what changed)
- ✅ Consistent interface (`bazel build`, `bazel run`, `bazel test`)
- ✅ CI/CD integration (same commands in CI as locally)

**One exception**: `grant_caps.sh` requires `sudo` to grant Linux capabilities. This is unavoidable and runs **after** building.

---

## All Bazel Targets

### Chaos Testing (`//chaos`)

| Target | Type | Purpose |
|--------|------|---------|
| `//chaos:simple_chaos_test` | `cc_binary` | Multi-threaded directory reading test |
| `//chaos:pause_controller` | `cc_binary` | Userspace eBPF controller |
| `//chaos:pause_injector_bpf` | `genrule` | eBPF bytecode (compiled with clang) |

### Common Libraries (`//common`)

| Target | Type | Purpose |
|--------|------|---------|
| `//common:dir_reader` | `cc_library` | DirectoryReader abstraction |

### VM Infrastructure (`//vm`)

| Target | Type | Purpose |
|--------|------|---------|
| `//vm:check_prerequisites` | `sh_binary` | Check host has QEMU/KVM/libvirt |
| `//vm:create_vm` | `sh_binary` | Create test VM |
| `//vm:deploy_rudra` | `sh_binary` | Deploy binaries to VM |
| `//vm:run_tests` | `sh_binary` | Run tests in VM |
| `//vm:destroy_vm` | `sh_binary` | Destroy test VM |

---

## Common Workflows

### 1. Initial Setup

```bash
# Clone repository
git clone https://github.com/your-org/rudra.git
cd rudra

# Build everything
bazel build //...

# Grant eBPF capabilities (one-time, requires sudo)
sudo ./grant_caps.sh
```

**After this, no more sudo needed!**

---

### 2. Host Testing (No VMs)

**Terminal 1 - Start eBPF Injector**:
```bash
bazel run //chaos:pause_controller -- 50 11
# Args: <probability%> <delay_iterations>
```

**Terminal 2 - Run Chaos Test**:
```bash
mkdir -p /tmp/rudra_test
touch /tmp/rudra_test/file{1..100}
bazel run //chaos:simple_chaos_test -- /tmp/rudra_test
```

**What happens**:
- Terminal 1 shows: `Delays injected: 150` (and counting)
- Terminal 2: Test completes (may be slower due to delays)
- ✅ Proves eBPF fault injection works!

---

### 3. VM Testing (Full Isolation)

**Check prerequisites**:
```bash
bazel run //vm:check_prerequisites
```

**Create VM**:
```bash
bazel run //vm:create_vm -- --name test-01
```

**Deploy RUDRA to VM**:
```bash
bazel run //vm:deploy_rudra -- test-01
```

**Run tests in VM**:
```bash
bazel run //vm:run_tests -- test-01
```

**Destroy VM when done**:
```bash
bazel run //vm:destroy_vm -- test-01
```

---

### 4. Development Workflow

**Build specific target**:
```bash
bazel build //chaos:pause_controller
```

**Run without rebuilding** (if already built):
```bash
bazel-bin/chaos/pause_controller 50 11
```

**Note**: After code changes, Bazel automatically rebuilds only what changed!

**Re-grant capabilities** (after rebuild):
```bash
sudo ./grant_caps.sh
```

---

### 5. CI/CD Workflow

**Build everything**:
```bash
bazel build //...
```

**Run tests** (when implemented):
```bash
bazel test //...
```

**Specific tests**:
```bash
bazel test //chaos:simple_chaos_test
bazel test //vm:vm_integration_test  # When implemented
```

---

## Why No Build Scripts?

**Before** (❌ Ad-hoc scripts):
```bash
./compile.sh              # What does this do?
./build.sh                # Calls compile.sh? Or different?
./run_chaos_test.sh       # Where are binaries?
./test_ebpf_works.sh      # Does this rebuild?
```

Problems:
- 🤷 Unclear dependencies
- 🔄 No incremental builds
- 🐛 Hard to debug
- 🚫 Not hermetic
- 😕 Different in CI vs local

**After** (✅ Bazel):
```bash
bazel build //...         # Build everything
bazel run //chaos:pause_controller -- 50 11
bazel run //vm:create_vm -- --name test-01
```

Benefits:
- ✅ Clear dependencies (in BUILD.bazel files)
- ✅ Incremental builds (only rebuild changed files)
- ✅ Easy to debug (`bazel build -s` shows commands)
- ✅ Hermetic (same results everywhere)
- ✅ Same in CI and locally

---

## Special Cases

### 1. eBPF Compilation

eBPF programs need special compilation with `clang -target bpf`:

```python
genrule(
    name = "pause_injector_bpf",
    srcs = ["pause_injector.bpf.c"],
    outs = ["pause_injector.bpf.o"],
    cmd = """
        clang -g -O2 -target bpf \
            -D__TARGET_ARCH_x86_64 \
            -I/usr/include/x86_64-linux-gnu \
            -c $(location pause_injector.bpf.c) \
            -o $@
    """,
)
```

This is a **genrule** (custom build rule), but it's still part of the Bazel build graph!

### 2. Linux Capabilities

Bazel **cannot** and **should not** grant Linux capabilities (requires root). This is an operational task:

```bash
sudo ./grant_caps.sh
```

This is the **only** script that exists outside Bazel, and it's necessary because:
- Requires root privileges
- System administration task (like `apt install`)
- One-time setup per binary

### 3. VM Operations

VM scripts are shell scripts, but exposed as Bazel targets:

```python
sh_binary(
    name = "create_vm",
    srcs = ["create_test_vm.sh"],
)
```

**Run via Bazel**:
```bash
bazel run //vm:create_vm -- --name test-01
```

**Why?**
- Consistent interface
- Explicit dependencies
- Part of build graph
- CI can use same commands

---

## Build Directory Structure

```
rudra/
├── BUILD.bazel                   # Root targets
├── WORKSPACE                     # Bazel workspace definition
├── MODULE.bazel                  # Bzlmod dependencies
│
├── bazel-bin/                    # Build outputs (gitignored)
│   ├── chaos/
│   │   ├── pause_controller      # ✅ Grant capabilities to this
│   │   ├── pause_injector.bpf.o
│   │   └── simple_chaos_test
│   ├── common/
│   │   └── libdir_reader.a
│   └── vm/
│       ├── check_prerequisites
│       ├── create_vm
│       └── ...
│
├── chaos/
│   ├── BUILD.bazel               # Chaos targets
│   ├── pause_controller.c
│   ├── pause_injector.bpf.c
│   └── simple_chaos_test.c
│
├── common/
│   ├── BUILD.bazel               # Library targets
│   ├── dir_reader.c
│   └── dir_reader.h
│
└── vm/
    ├── BUILD.bazel               # VM targets
    ├── check_prerequisites.sh
    ├── create_test_vm.sh
    └── ...
```

---

## Quick Reference

| Task | Command |
|------|---------|
| Build everything | `bazel build //...` |
| Build specific target | `bazel build //chaos:pause_controller` |
| Run binary | `bazel run //chaos:pause_controller -- ARGS` |
| Grant capabilities | `sudo ./grant_caps.sh` |
| Run chaos test | `bazel run //chaos:simple_chaos_test -- /tmp/rudra_test` |
| Check VM prereqs | `bazel run //vm:check_prerequisites` |
| Create VM | `bazel run //vm:create_vm -- --name test-01` |
| List all targets | `bazel query //...` |
| Clean build | `bazel clean` |
| Show build commands | `bazel build -s //chaos:pause_controller` |

---

## Troubleshooting

### "bazel: command not found"

Install Bazel:
```bash
# Ubuntu/Debian
sudo apt install bazel

# Or use Bazelisk (version manager)
npm install -g @bazel/bazelisk
```

### "Permission denied" after rebuild

Bazel creates new binaries on each build, losing capabilities:

```bash
# After any code change and rebuild
sudo ./grant_caps.sh
```

### "Failed to load eBPF program"

Check capabilities:
```bash
getcap bazel-bin/chaos/pause_controller
# Should show: cap_bpf,cap_perfmon,cap_net_admin,cap_sys_admin=ep
```

If missing:
```bash
sudo ./grant_caps.sh
```

---

## Further Reading

- **Bazel Concepts**: https://bazel.build/concepts/build-ref
- **BUILD.bazel Syntax**: https://bazel.build/reference/be/functions
- **eBPF with Bazel**: See `chaos/BUILD.bazel` for genrule example
- **Shell Scripts in Bazel**: See `vm/BUILD.bazel` for sh_binary examples

---

*RUDRA: Everything through Bazel, nothing hidden* 🚀

