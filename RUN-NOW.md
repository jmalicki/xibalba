# 🚀 Run RUDRA eBPF Test - Simple Steps

## Quick 3-Step Process

### Step 1: Compile (no sudo needed)

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

### Step 2: Grant Capabilities (sudo, one-time)

```bash
sudo ./grant_caps.sh
```

**Output**:
```
✅ Granted eBPF capabilities to bin/pause_controller
bin/pause_controller cap_bpf,cap_perfmon,cap_net_admin=ep
```

---

### Step 3: Test (Two Terminals)

**Terminal 1** - Start Pause Controller (no sudo!):
```bash
./run_pause_controller.sh 20 5000
```

**Terminal 2** - Run Chaos Test:
```bash
./run_chaos_test.sh /tmp/rudra_test
```

---

## Expected Results

**Terminal 1 shows**:
```
🚀 Pause controller active!
[CPU 2] Pause request: pid=12345, tid=12346, ts=...
  ✓ Paused pid=12345 for 5000μs (total: 1)
... (hundreds of these)
```

**Terminal 2 shows**:
```
Operations/second: ~25,000-30,000
(slower than 38,000 baseline = pauses working!)
✅ PASS: No crashes detected
```

---

## ✅ Success = eBPF Works!

If you see pauses in Terminal 1 → **Tech de-risking validated!** 🎉

---

*All binaries in `bin/` directory (part of project, persistent across reboots)*

