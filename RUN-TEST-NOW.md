# Run eBPF Test - Two Terminal Instructions

## Terminal 1: Pause Controller

```bash
# Grant capabilities (one-time)
sudo setcap cap_bpf,cap_perfmon,cap_net_admin=ep /tmp/pause_controller

# Verify
getcap /tmp/pause_controller
# Should show: cap_bpf,cap_perfmon,cap_net_admin=ep

# Run pause controller
cd /tmp
./pause_controller 20 5000
```

**Expected output**:
```
=== RUDRA Pause Controller ===
Pause probability: 20%
Pause duration: 5000μs

Loading eBPF program...
  ✓ eBPF program loaded
  ✓ Attached: trace_getdents64_entry
  ✓ Configuration set: 20% pause probability
  ✓ Perf buffer created

🚀 Pause controller active!
   Monitoring getdents64 syscalls...
   Will pause 20% of calls for 5000μs each

Press Ctrl+C to stop
```

**Leave this running!**

---

## Terminal 2: Run Chaos Test

```bash
# Run the test
/tmp/simple_chaos_test /tmp/rudra_test
```

**What should happen**:
- Test runs (takes ~5 seconds)
- Should be **slower** than baseline (38K ops/sec → ~25-30K ops/sec)
- Terminal 1 should show:
  ```
  [CPU 2] Pause request: pid=12345, tid=12346, ts=...
    ✓ Paused pid=12345 for 5000μs (total: 1)
  [CPU 1] Pause request: pid=12345, tid=12347, ts=...
    ✓ Paused pid=12345 for 5000μs (total: 2)
  ...
  (many more pause events)
  ```

---

## Success Criteria

✅ **Terminal 1**: Shows "Pause request" messages (hundreds of them)  
✅ **Terminal 2**: Test completes (slower than 38K ops/sec)  
✅ **Result**: eBPF pause injection works!

---

## If It Doesn't Work

**Check capabilities**:
```bash
getcap /tmp/pause_controller
# Must show: cap_bpf,cap_perfmon,cap_net_admin=ep
```

**Check eBPF loaded**:
```bash
bpftool prog list | grep getdents
# Should show program ID if loaded
```

**Check kernel messages**:
```bash
sudo dmesg | tail -20
# Look for eBPF errors
```

---

**Ready? Open two terminals and follow the steps above!** 🚀


