# Xibalba Tech De-Risking - Current Status

*Progress report for PoC implementation*
*Date: October 10, 2025*

## ✅ Phase 0: Prerequisites - COMPLETE

**Kernel environment**:
- ✅ Kernel 6.14.0-33-generic (excellent eBPF support!)
- ✅ BTF available (/sys/kernel/btf/vmlinux - 6.5MB)
- ✅ CONFIG_BPF_KPROBE_OVERRIDE=y (can use bpf_override_return!)
- ✅ clang installed (/usr/bin/clang)
- ✅ bpftool installed (/usr/sbin/bpftool)
- ✅ libbpf-dev installed
- ✅ linux-headers installed

**Status**: Perfect environment for eBPF development! ✅

---

## ✅ Phase 1: Minimal DirectoryReader - COMPLETE

**Files created**:
- ✅ `common/dir_reader.h` - Header with minimal interface
- ✅ `common/dir_reader.c` - Implementation using classic readdir()
- ✅ `common/BUILD.bazel` - Updated for minimal build

**Compilation**: ✅ Compiles cleanly with gcc

**Status**: DirectoryReader abstraction working!

---

## ✅ Phase 2: Simple Chaos Test - COMPLETE

**Files created**:
- ✅ `chaos/simple_chaos_test.c` - 10 concurrent readers
- ✅ `chaos/BUILD.bazel` - Updated with simple_chaos_test target

**Test results**:
```
Directory: /tmp/xibalba_test (100 files)
Threads: 10
Duration: 5 seconds

Operations completed: 189,921
Total entries read: 19,371,942
Operations/second: 37,984

✅ PASS: No crashes detected
```

**Status**: Concurrent reading works perfectly! Baseline established.

---

## 🚧 Phase 3: eBPF Pause Injection - IN PROGRESS

**Files created**:
- ✅ `chaos/pause_injector.bpf.c` - eBPF program that hooks getdents64
- ✅ `chaos/pause_controller.c` - Userspace controller
- ✅ Compiled to: `/tmp/pause_injector.bpf.o` and `/tmp/pause_controller`

**Current status**: Code ready, needs manual testing with sudo

**Next step**: You need to run this manually:

```bash
# Terminal 1: Start pause controller (needs sudo for eBPF)
cd /tmp
sudo ./pause_controller 20  # 20% pause probability

# Should see:
# === Xibalba Pause Controller ===
# Pause probability: 20%
# ✓ eBPF program loaded
# ✓ Attached: trace_getdents64_entry
# 🚀 Pause controller active!

# Terminal 2: Run chaos test
/tmp/simple_chaos_test /tmp/xibalba_test

# Terminal 1 should show pause events!
```

---

## Expected Results

**Without eBPF** (baseline):
- Operations/sec: ~38,000
- No pauses

**With eBPF (20% pause rate, 5ms pauses)**:
- Operations/sec: ~25,000-30,000 (slower due to pauses)
- Should see "Pause request" messages in Terminal 1
- Should see "✓ Paused pid=..." messages

**If successful**: ✅ eBPF pause injection works!

---

## Files Created (Summary)

```
xibalba/
├── common/
│   ├── BUILD.bazel         (updated)
│   ├── dir_reader.h        (new, 30 lines)
│   └── dir_reader.c        (new, 70 lines)
├── chaos/
│   ├── BUILD.bazel         (updated)
│   ├── simple_chaos_test.c (new, 110 lines)
│   ├── pause_injector.bpf.c (new, 60 lines)
│   └── pause_controller.c  (new, 160 lines)
└── docs/
    ├── TECH-DERISKING-PLAN.md
    ├── RISKS-AND-OPEN-QUESTIONS.md
    └── (updated implementation docs)

Total new code: ~430 lines
```

---

## Next Steps

### Immediate (Right Now):

1. **Test eBPF pause injection** (manual, needs sudo):
   ```bash
   # Terminal 1
   cd /tmp && sudo ./pause_controller 20
   
   # Terminal 2
   /tmp/simple_chaos_test /tmp/xibalba_test
   ```

2. **Verify pauses are working**:
   - Check Terminal 1 for pause event messages
   - Operations/sec should be slower (~25-30K vs 38K)

### If eBPF Works:

3. Create race detector test (Phase 5 of de-risking plan)
4. Prove pauses increase race detection
5. Document results
6. Decide: proceed to full implementation?

### If eBPF Doesn't Work:

3. Debug eBPF loading:
   ```bash
   sudo dmesg | tail -20  # Check for eBPF errors
   sudo bpftool prog list  # See if program loaded
   ```

4. Check for issues:
   - Tracepoint not available?
   - Permission issues?
   - eBPF verifier rejection?

---

## Installation Notes

**Still need to install**:
- Bazel (optional for now - gcc works for PoC)
  ```bash
  # Install Bazelisk (recommended)
  wget https://github.com/bazelbuild/bazelisk/releases/download/v1.19.0/bazelisk-linux-amd64
  chmod +x bazelisk-linux-amd64
  sudo mv bazelisk-linux-amd64 /usr/local/bin/bazel
  ```

**Already have**:
- ✅ clang, llvm, bpftool
- ✅ libbpf-dev
- ✅ gcc, linux-headers
- ✅ All eBPF prerequisites

---

## Quick Reference Commands

```bash
# Compile everything
cd /home/jmalicki/src/xibalba
gcc -o /tmp/simple_chaos_test chaos/simple_chaos_test.c common/dir_reader.c -I. -pthread
clang -g -O2 -target bpf -D__TARGET_ARCH_x86_64 -c chaos/pause_injector.bpf.c -o /tmp/pause_injector.bpf.o
gcc -o /tmp/pause_controller chaos/pause_controller.c -lbpf -lelf

# Run baseline test (no eBPF)
/tmp/simple_chaos_test /tmp/xibalba_test

# Run with eBPF pauses (two terminals needed)
# Terminal 1:
cd /tmp && sudo ./pause_controller 20

# Terminal 2:
/tmp/simple_chaos_test /tmp/xibalba_test
```

---

*Status: Phase 0-2 complete, Phase 3 ready for manual testing!*

