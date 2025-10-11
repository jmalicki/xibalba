# RUDRA Tech De-Risking: Quick Start

*Get eBPF fault injection working in 5 minutes*

## What We've Built

✅ **Minimal DirectoryReader** - Abstracts directory reading  
✅ **Simple Chaos Test** - Multi-threaded concurrent directory reading  
✅ **eBPF Delay Injector** - Injects random delays into `getdents64()`  
✅ **Pause Controller** - Userspace controller for eBPF program  

**Total code**: ~500 lines

---

## Quick Start (3 Steps)

### Step 1: Build Everything

```bash
cd rudra
bazel build //...
```

**What this does**:
- Compiles DirectoryReader library
- Compiles chaos test
- Compiles eBPF program (pause_injector.bpf.o)
- Compiles pause controller

**Expected output**:
```
INFO: Analyzed 3 targets (5 packages loaded, 42 targets configured).
INFO: Found 3 targets...
INFO: Build completed successfully, 12 total actions
```

---

### Step 2: Grant eBPF Capabilities (One-Time)

```bash
sudo ./grant_caps.sh
```

**What this does**:
- Grants `CAP_BPF`, `CAP_PERFMON`, `CAP_NET_ADMIN`, `CAP_SYS_ADMIN` to `pause_controller`
- **After this, no more sudo needed!**

**Expected output**:
```
=== Granting eBPF Capabilities ===

Target: /home/.../rudra/bazel-bin/chaos/pause_controller

Capabilities:
  • CAP_BPF       : Load eBPF programs
  • CAP_PERFMON   : Attach to tracepoints/kprobes
  • CAP_NET_ADMIN : Network-related BPF operations
  • CAP_SYS_ADMIN : Kprobe attachment

✅ Capabilities granted successfully

bazel-bin/chaos/pause_controller cap_bpf,cap_perfmon,cap_net_admin,cap_sys_admin=ep

Now you can run without sudo:
  bazel run //chaos:pause_controller -- 50 11
```

---

### Step 3: Test eBPF Works (Two Terminals)

**Terminal 1 - Start eBPF Injector**:
```bash
bazel run //chaos:pause_controller -- 50 11
```

**What this does**:
- Loads eBPF program that hooks `getdents64()` syscalls
- Injects delays with 50% probability
- Uses 11 busy-loop iterations (~0.5μs each)

**Expected output**:
```
=== RUDRA Delay Injector ===
Delay probability: 50%
Delay iterations: 11 (approx 0μs)

Loading eBPF program from: chaos/pause_injector.bpf.o
  ✓ eBPF program loaded
  ✓ Attached to tracepoint: syscalls/sys_enter_getdents64
  ✓ Configuration set (probability=50%, iterations=11)

🚀 Monitoring getdents64() syscalls...
   Press Ctrl+C to stop

Delays injected: 0
```

**Leave this running!**

---

**Terminal 2 - Run Chaos Test**:
```bash
# Create test directory
mkdir -p /tmp/rudra_test
touch /tmp/rudra_test/file{1..100}

# Run test
bazel run //chaos:simple_chaos_test -- /tmp/rudra_test
```

**What should happen**:
- Test runs 10 concurrent reader threads for 5 seconds
- Terminal 1 shows: `Delays injected: 150` (and counting up!)
- Test completes successfully

**Expected output (Terminal 2)**:
```
=== Simple Chaos Test ===
Path: /tmp/rudra_test
Threads: 10
Duration: 5s

Test running...
Done.

Results:
  Total entries read: 1,000
  Operations completed: 150
  Duration: 5.12s
  Throughput: 29.3 ops/sec

✅ Test completed successfully
```

---

## Success Criteria

✅ **Terminal 1**: Shows "Delays injected: N" (N > 0 and increasing)  
✅ **Terminal 2**: Test completes without errors  
✅ **Result**: eBPF fault injection is working!

---

## What Capabilities Were Granted?

The `pause_controller` binary gets these Linux capabilities:

| Capability | Why Needed |
|------------|------------|
| **CAP_BPF** | Load and attach eBPF programs |
| **CAP_PERFMON** | Attach to tracepoints/kprobes |
| **CAP_NET_ADMIN** | Network-related BPF operations |
| **CAP_SYS_ADMIN** | Kprobe attachment |

**Security note**: These are powerful capabilities. Only use on dev/test machines.

**Check current capabilities**:
```bash
getcap bazel-bin/chaos/pause_controller
```

**Remove capabilities** (if needed):
```bash
sudo setcap -r bazel-bin/chaos/pause_controller
```

---

## Troubleshooting

### Issue: "bazel: command not found"

**Install Bazel**:
```bash
# Ubuntu/Debian
sudo apt install apt-transport-https curl gnupg
curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor > bazel-archive-keyring.gpg
sudo mv bazel-archive-keyring.gpg /usr/share/keyrings
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] https://storage.googleapis.com/bazel-apt stable jdk1.8" | sudo tee /etc/apt/sources.list.d/bazel.list
sudo apt update && sudo apt install bazel
```

**Or use Bazelisk** (version manager):
```bash
npm install -g @bazel/bazelisk
```

---

### Issue: "eBPF program not loaded"

**Check kernel support**:
```bash
uname -r  # Need 5.10+
ls /sys/kernel/btf/vmlinux  # Should exist
```

**Check dependencies**:
```bash
# Install eBPF development tools
sudo apt install clang libbpf-dev linux-headers-$(uname -r)
```

---

### Issue: "Permission denied" even after grant_caps.sh

**Re-run after rebuild**:

Bazel creates new binaries on each build, losing capabilities. After any code change:

```bash
bazel build //chaos:pause_controller
sudo ./grant_caps.sh  # Grant capabilities again
```

**Alternative**: Build once, grant capabilities, then run without rebuilding.

---

### Issue: "No delays injected" (Terminal 1 shows 0)

**Possible causes**:

1. **Test not using getdents64**: Verify with strace
   ```bash
   strace -e getdents64 bazel-bin/chaos/simple_chaos_test /tmp/rudra_test
   ```

2. **Probability too low**: Try 100%
   ```bash
   bazel run //chaos:pause_controller -- 100 11
   ```

3. **Wrong tracepoint**: Check available tracepoints
   ```bash
   ls /sys/kernel/debug/tracing/events/syscalls/ | grep getdents
   ```

---

## Build Tips

**Clean build**:
```bash
bazel clean
bazel build //...
```

**Build specific target**:
```bash
bazel build //chaos:pause_controller
bazel build //chaos:simple_chaos_test
bazel build //common:dir_reader
```

**Run without building**:
```bash
# Run directly from bazel-bin
bazel-bin/chaos/simple_chaos_test /tmp/rudra_test
```

---

## Next Steps

Once you see delays being injected:

1. **Increase delay iterations** to widen race windows:
   ```bash
   bazel run //chaos:pause_controller -- 50 500
   # 500 iterations ≈ 25μs delay
   ```

2. **Add race detection** to chaos test:
   - Detect duplicate entries
   - Check for missing entries
   - Validate ordering

3. **Measure effectiveness**:
   - Baseline: races found without delays
   - With delays: races found with delays
   - Prove: delays → more races found

4. **Move to VMs** for kernel patch testing:
   ```bash
   # Check VM prerequisites
   bazel run //vm:check_prerequisites
   
   # Create and test in VM
   bazel run //vm:create_vm -- --name test-01
   bazel run //vm:deploy_rudra -- test-01
   bazel run //vm:run_tests -- test-01
   ```

---

## Files Reference

```
rudra/
├── bazel-bin/                         (Bazel output)
│   └── chaos/
│       ├── pause_injector.bpf.o       eBPF bytecode
│       ├── pause_controller           Userspace controller (grant capabilities to this)
│       └── simple_chaos_test          Chaos test binary
├── chaos/
│   ├── BUILD.bazel                    Build rules for chaos testing
│   ├── pause_injector.bpf.c           eBPF source (compiled to .bpf.o)
│   ├── pause_controller.c             Userspace controller source
│   └── simple_chaos_test.c            Chaos test source
├── common/
│   ├── BUILD.bazel                    Build rules for common libraries
│   ├── dir_reader.c                   DirectoryReader implementation
│   └── dir_reader.h                   DirectoryReader header
├── grant_caps.sh                      Grant capabilities (sudo required)
└── WORKSPACE                          Bazel workspace definition
```

---

## Time Investment

- **Build**: ~10 seconds
- **Grant capabilities**: ~5 seconds (one-time)
- **Test**: ~10 seconds
- **Total**: ~25 seconds to validate eBPF works ⚡

---

*Run `bazel build //...` to get started!*
