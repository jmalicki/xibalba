# RUDRA Tech De-Risking: Quick Start

*Get eBPF pause injection working in 15 minutes*

## What We've Built So Far

✅ **Minimal DirectoryReader** - Abstracts directory reading  
✅ **Simple Chaos Test** - 10 concurrent readers, 5 seconds  
✅ **eBPF Pause Injector** - Hooks getdents64, requests pauses  
✅ **Pause Controller** - Receives eBPF events, pauses processes  

**Total code**: ~430 lines

---

## Quick Start (3 Commands)

### 1. Setup eBPF Permissions (One-Time)

```bash
sudo ./setup_ebpf_permissions.sh
```

**What this does**:
- Compiles all binaries to `bin/`
- Grants CAP_BPF capabilities to pause_controller
- Creates convenience scripts
- **After this, no more sudo needed!**

---

### 2. Test eBPF Works (Automated)

```bash
./test_ebpf_works.sh
```

**What this does**:
- Runs baseline test (no eBPF)
- Runs test with eBPF pauses (20%)
- Compares results
- Shows if pauses were injected

**Expected output**:
```
Baseline (no eBPF):  38,000 ops/sec
With eBPF (20%):     25,000 ops/sec  (slower = pauses working!)
Pauses injected:     500+

✅ SUCCESS: eBPF pause injection is working!
```

---

### 3. Manual Testing (Two Terminals)

If you want to see it live:

**Terminal 1**: Start pause controller
```bash
./run_pause_controller.sh 20
# 20% pause probability, 5ms pauses
# No sudo needed!
```

**Terminal 2**: Run chaos test
```bash
mkdir -p /tmp/rudra_test
touch /tmp/rudra_test/file{1..100}
./run_chaos_test.sh /tmp/rudra_test
```

**Terminal 1 will show**:
```
🚀 Pause controller active!
[CPU 2] Pause request: pid=12345, tid=12346, ts=...
  ✓ Paused pid=12345 for 5000μs
[CPU 1] Pause request: pid=12345, tid=12347, ts=...
  ✓ Paused pid=12345 for 5000μs
...
```

---

## What Capabilities Were Granted?

The `pause_controller` binary gets these Linux capabilities:

| Capability | Why Needed |
|------------|------------|
| **CAP_BPF** | Load and attach eBPF programs |
| **CAP_PERFMON** | Access perf events (for eBPF → userspace communication) |
| **CAP_NET_ADMIN** | Attach to network-related tracepoints |

**Security note**: These are powerful capabilities. Only use on dev/test machines.

**Check current capabilities**:
```bash
getcap bin/pause_controller
# Output: bin/pause_controller cap_bpf,cap_perfmon,cap_net_admin=ep
```

**Remove capabilities** (if needed):
```bash
sudo setcap -r bin/pause_controller
```

---

## Troubleshooting

### Issue: "eBPF program not loaded"

**Check**:
```bash
# 1. Verify kernel support
uname -r  # Need 5.10+
ls /sys/kernel/btf/vmlinux  # Should exist

# 2. Check bpftool works
sudo bpftool prog list

# 3. Check logs
cat /tmp/pause_controller.log

# 4. Check kernel messages
sudo dmesg | tail -20
```

---

### Issue: "Failed to attach: trace_getdents64_entry"

**Cause**: Tracepoint doesn't exist

**Fix**: Check available tracepoints
```bash
ls /sys/kernel/debug/tracing/events/syscalls/ | grep getdents

# Should see:
# sys_enter_getdents64
# sys_exit_getdents64
```

---

### Issue: "Permission denied" even after setup

**Check capabilities**:
```bash
getcap bin/pause_controller
# Should show: cap_bpf,cap_perfmon,cap_net_admin=ep
```

**Re-run setup**:
```bash
sudo ./setup_ebpf_permissions.sh
```

---

### Issue: "No pauses detected"

**Possible causes**:

1. **Pause probability too low**: Try higher (50%)
   ```bash
   ./run_pause_controller.sh 50
   ```

2. **Test too short**: Run longer test
   ```bash
   # Modify simple_chaos_test.c: TEST_DURATION 30
   ```

3. **Wrong syscall hooked**: Check test actually uses getdents64
   ```bash
   strace -e getdents64 ./run_chaos_test.sh /tmp/rudra_test
   ```

---

## Next Steps After This Works

Once you see pauses being injected:

1. **Phase 5**: Create race detector
   - Detect duplicate entries
   - Prove pauses increase race detection

2. **Phase 6**: Add bpf_override_return() for fault injection
   - Inject -EAGAIN, -ENOMEM
   - Test error handling

3. **Phase 7**: Document results
   - Validate approach works
   - Decide: proceed to full implementation?

---

## Current Status

**Completed**:
- ✅ Phase 0: Prerequisites
- ✅ Phase 1: Minimal DirectoryReader  
- ✅ Phase 2: Simple chaos test
- ✅ Phase 3: eBPF pause injector (code ready)

**Testing now**:
- 🧪 Validate eBPF pause injection works
- 🧪 Prove pauses can be injected without sudo

**Next**: Find a race condition!

---

## Files Reference

```
rudra/
├── bin/                          (created by setup script)
│   ├── pause_injector.bpf.o      eBPF bytecode
│   ├── pause_controller          Userspace controller (has capabilities)
│   └── simple_chaos_test         Chaos test binary
├── setup_ebpf_permissions.sh     Run once: sudo ./setup...
├── test_ebpf_works.sh            Automated test
├── run_pause_controller.sh       Convenience: ./run... 20
└── run_chaos_test.sh             Convenience: ./run... /tmp/dir
```

---

**Time to run setup**: ~1 minute  
**Time to test**: ~30 seconds  
**Total time to validate eBPF works**: ~2 minutes ⚡

---

*Run `sudo ./setup_ebpf_permissions.sh` to get started!*

