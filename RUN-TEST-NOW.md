# Run eBPF Test - Simple 3-Step Process

## Step 1: Fix bin/ Permissions (if needed)

If `bin/` was created by root earlier:
```bash
sudo chown -R $USER:$USER bin/
```

Or just remove and recreate:
```bash
sudo rm -rf bin/
```

---

## Step 2: Compile Everything

```bash
./compile.sh
```

**Output**:
```
=== Compiling RUDRA Binaries ===
Output directory: /home/jmalicki/src/rudra/bin

Compiling simple_chaos_test...
  ✓ bin/simple_chaos_test
Compiling pause_controller...
  ✓ bin/pause_controller
Compiling pause_injector.bpf.o...
  ✓ bin/pause_injector.bpf.o

=== Build Complete ===
```

---

## Step 3: Grant Capabilities (One-Time, Needs sudo)

```bash
sudo ./grant_caps.sh
```

**Output**:
```
✅ Granted eBPF capabilities to bin/pause_controller
bin/pause_controller cap_bpf,cap_perfmon,cap_net_admin=ep
```

**After this, no more sudo needed!**

---

## Step 4: Run Test (Two Terminals)

### Terminal 1: Start Pause Controller

```bash
./run_pause_controller.sh 20 5000
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

### Terminal 2: Run Chaos Test

```bash
./run_chaos_test.sh /tmp/rudra_test
```

**Expected output**:
```
=== RUDRA Simple Chaos Test ===
Directory: /tmp/rudra_test
Threads: 10
Duration: 5 seconds

Starting test...
Stopping threads...

=== Results ===
Operations completed: ~150,000 (fewer than 190k = pauses working!)
Operations/second: ~25,000-30,000 (vs 38,000 baseline)

✅ PASS: No crashes detected
```

**Watch Terminal 1** - should show hundreds of:
```
[CPU 2] Pause request: pid=12345, tid=12346, ts=...
  ✓ Paused pid=12345 for 5000μs (total: 1)
[CPU 1] Pause request: pid=12345, tid=12347, ts=...
  ✓ Paused pid=12345 for 5000μs (total: 2)
... (many more)
```

---

## Success Criteria

✅ **Terminal 1**: Shows hundreds of "Pause request" messages  
✅ **Terminal 2**: Slower performance (~25-30K vs 38K ops/sec)  
✅ **Result**: eBPF pause injection WORKS!

---

## Troubleshooting

**If compile.sh fails with "Permission denied"**:
```bash
sudo rm -rf bin/
./compile.sh
```

**If grant_caps.sh fails**:
```bash
# Make sure pause_controller exists
ls -la bin/pause_controller
```

**If eBPF doesn't load**:
```bash
# Check kernel support
grep CONFIG_BPF_KPROBE_OVERRIDE /boot/config-$(uname -r)

# Check dmesg for errors
sudo dmesg | tail -20
```

---

*All binaries go to `bin/` directory (persistent, part of project)*

