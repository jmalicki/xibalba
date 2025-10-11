# Run eBPF Test - Two Terminal Instructions

*Test eBPF fault injection in under 1 minute*

---

## Prerequisites

1. **Build everything**:
   ```bash
   bazel build //...
   ```

2. **Grant capabilities** (one-time):
   ```bash
   sudo ./grant_caps.sh
   ```

3. **Create test directory**:
   ```bash
   mkdir -p /tmp/rudra_test
   touch /tmp/rudra_test/file{1..100}
   ```

---

## Terminal 1: eBPF Fault Injector

```bash
bazel run //chaos:pause_controller -- 50 11
```

**Arguments**:
- `50` = 50% probability of injecting delay
- `11` = 11 busy-loop iterations (~0.5μs delay)

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
Delays injected: 45
Delays injected: 98
Delays injected: 152
...
```

**What this means**:
- eBPF program is hooked into `getdents64()` syscall
- Every time any process calls `getdents64()`, the eBPF program runs
- 50% of the time, it injects a small delay
- Counter shows how many delays have been injected

**Leave this running!**

---

## Terminal 2: Run Chaos Test

```bash
bazel run //chaos:simple_chaos_test -- /tmp/rudra_test
```

**Expected output**:
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

**What should happen in Terminal 1**:
- Counter increases rapidly: `Delays injected: 45`, `98`, `152`...
- Proves eBPF is intercepting syscalls and injecting delays

---

## Success Criteria

✅ **Terminal 1**: "Delays injected" counter increases (> 0)  
✅ **Terminal 2**: Test completes without errors  
✅ **Result**: eBPF fault injection is working!

---

## Adjusting Delay Parameters

**More aggressive delays** (widen race windows):
```bash
bazel run //chaos:pause_controller -- 50 500
# 50% probability, 500 iterations (~25μs delay)
```

**Always inject delays** (100% probability):
```bash
bazel run //chaos:pause_controller -- 100 11
# Every getdents64() call gets delayed
```

**Rare but long delays**:
```bash
bazel run //chaos:pause_controller -- 10 1000
# 10% probability, 1000 iterations (~50μs delay)
```

**Trade-off**:
- Higher probability → More slowdown, more race opportunities
- Higher iterations → Longer delays, wider race windows
- Balance: enough to find races, not so much test never finishes

---

## Alternative: Run from bazel-bin

If you've already built and granted capabilities, you can run directly:

**Terminal 1**:
```bash
bazel-bin/chaos/pause_controller 50 11
```

**Terminal 2**:
```bash
bazel-bin/chaos/simple_chaos_test /tmp/rudra_test
```

**Note**: After any code change and rebuild, you must re-run `sudo ./grant_caps.sh` because Bazel creates new binaries.

---

## Troubleshooting

### Issue: "Failed to load eBPF program"

**Check capabilities**:
```bash
getcap bazel-bin/chaos/pause_controller
# Should show: cap_bpf,cap_perfmon,cap_net_admin,cap_sys_admin=ep
```

**If missing, re-grant**:
```bash
sudo ./grant_caps.sh
```

---

### Issue: "Delays injected: 0" (stays at 0)

**Possible causes**:

1. **Test not using getdents64**:
   ```bash
   strace -e getdents64 bazel-bin/chaos/simple_chaos_test /tmp/rudra_test 2>&1 | grep getdents64
   # Should see multiple getdents64() calls
   ```

2. **Wrong tracepoint**:
   ```bash
   ls /sys/kernel/debug/tracing/events/syscalls/ | grep getdents
   # Should show: sys_enter_getdents64, sys_exit_getdents64
   ```

3. **eBPF not attached**:
   ```bash
   sudo bpftool prog list | grep getdents
   # Should show a program if loaded
   ```

---

### Issue: "Permission denied" when loading eBPF

**Check kernel config**:
```bash
# Need unprivileged eBPF enabled OR capabilities
cat /proc/sys/kernel/unprivileged_bpf_disabled
# 0 = unprivileged allowed
# 1 = privileged only (use capabilities)
```

**Check capabilities granted**:
```bash
getcap bazel-bin/chaos/pause_controller
```

---

### Issue: Test fails or crashes

**Check kernel logs**:
```bash
sudo dmesg | tail -20
# Look for eBPF verifier errors or kernel warnings
```

**Run with debug**:
```bash
LIBBPF_DEBUG=1 bazel run //chaos:pause_controller -- 50 11
# Shows detailed eBPF loading process
```

---

## Understanding the Output

**Terminal 1 (pause_controller)**:
```
Delays injected: 152
```
- This counter shows how many `getdents64()` syscalls were delayed
- Higher number = more delays injected
- Should increase rapidly when Terminal 2 runs

**Terminal 2 (chaos test)**:
```
Throughput: 29.3 ops/sec
```
- Operations per second (directory scans)
- **With eBPF delays**: Slower (20-30 ops/sec)
- **Without eBPF**: Faster (40-50 ops/sec)
- Slowdown proves delays are working!

---

## After It Works

Once you see delays being injected:

1. **Add race detection** to `simple_chaos_test.c`:
   - Check for duplicate entries
   - Check for missing entries
   - Prove delays increase race detection rate

2. **Try different filesystems**:
   - `/tmp` (tmpfs)
   - `/home` (ext4/xfs)
   - NFS mount (if available)

3. **Test with concurrent writers**:
   - Modify test to create/delete files during reading
   - See if delays expose race conditions

4. **Move to VMs**:
   - Controlled environment
   - Custom kernel builds
   - Test kernel patches

---

## Quick Reference

| Command | Purpose |
|---------|---------|
| `bazel build //...` | Build all code |
| `sudo ./grant_caps.sh` | Grant eBPF capabilities (after build) |
| `bazel run //chaos:pause_controller -- 50 11` | Run eBPF injector |
| `bazel run //chaos:simple_chaos_test -- /tmp/rudra_test` | Run chaos test |
| `getcap bazel-bin/chaos/pause_controller` | Check capabilities |
| `sudo bpftool prog list` | List loaded eBPF programs |

---

**Ready? Open two terminals and follow the steps above!** 🚀
