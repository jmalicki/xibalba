# How eBPF Pauses Work in Xibalba

*Understanding the pause injection mechanism*

---

## The Jepsen Analogy

**Jepsen**: Nemesis directly causes chaos (network partitions, process kills)  
**Xibalba**: eBPF directly causes chaos (pauses at critical kernel functions)

**Key insight**: The pause must happen **AT THE MOMENT** of the hook, not later!

---

## The Problem with Current Design (Wrong Approach)

**Current implementation** (pause_injector.bpf.c):
```
1. eBPF hook fires (getdents64 syscall entry)
2. eBPF sends event to userspace via perf buffer
3. Userspace receives event
4. Userspace sends SIGSTOP to process
5. Process pauses
6. Userspace waits
7. Userspace sends SIGCONT
8. Process resumes
```

**Problems**:
- ⏱️ **Latency**: Round-trip to userspace adds milliseconds
- 🎯 **Wrong timing**: Pause happens AFTER the critical moment
- 🔄 **Complexity**: Requires userspace coordination

**This is NOT how Jepsen works!** Jepsen directly causes the fault.

---

## The Correct Approach: Direct eBPF Delays

**What we should do**:
```
1. eBPF hook fires (at critical kernel function)
2. eBPF IMMEDIATELY pauses execution right there
3. Busy-wait for N microseconds
4. Return and continue execution
```

**eBPF can't call sleep()**, but it CAN busy-wait:

```c
// Busy-wait for delay_us microseconds
static __always_inline void bpf_busy_wait(__u64 delay_us) {
    __u64 start = bpf_ktime_get_ns();
    __u64 end = start + (delay_us * 1000);  // Convert to nanoseconds
    
    // Busy loop until time elapsed
    while (bpf_ktime_get_ns() < end) {
        // Just spin - this burns CPU but pauses execution
    }
}
```

---

## Corrected pause_injector.bpf.c

```c
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

// Configuration
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, __u32);
} config SEC(".maps");

#define CFG_PAUSE_PROBABILITY 0  // Key for pause probability (%)
#define CFG_PAUSE_DURATION_US 1  // Key for pause duration (microseconds)

// Statistics (optional)
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

// Busy-wait to create a pause
static __always_inline void bpf_pause(__u32 delay_us) {
    __u64 start_ns = bpf_ktime_get_ns();
    __u64 end_ns = start_ns + ((__u64)delay_us * 1000);
    
    // Busy-wait loop
    // This burns CPU but creates the pause we want at the exact moment
    while (bpf_ktime_get_ns() < end_ns) {
        // Spin - this is intentional!
        // We want to pause RIGHT HERE at this critical moment
    }
}

// Hook: getdents64 syscall entry
SEC("tracepoint/syscalls/sys_enter_getdents64")
int trace_getdents64_entry(void *ctx) {
    // Read config
    __u32 key_prob = CFG_PAUSE_PROBABILITY;
    __u32 *pause_prob = bpf_map_lookup_elem(&config, &key_prob);
    if (!pause_prob || *pause_prob == 0)
        return 0;
    
    __u32 key_dur = CFG_PAUSE_DURATION_US;
    __u32 *pause_duration = bpf_map_lookup_elem(&config, &key_dur);
    if (!pause_duration || *pause_duration == 0)
        return 0;
    
    // Random decision: should we pause?
    __u32 rand = bpf_get_prandom_u32();
    if ((rand % 100) >= *pause_prob)
        return 0;  // Not this time
    
    // PAUSE RIGHT HERE!
    // This happens at the EXACT moment of getdents64 entry
    // Perfect for catching race conditions
    bpf_pause(*pause_duration);
    
    // Update statistics
    __u32 stat_key = 0;
    __u64 *count = bpf_map_lookup_elem(&stats, &stat_key);
    if (count)
        __sync_fetch_and_add(count, 1);
    
    return 0;
}
```

---

## Why This Works Better

**Jepsen-Style Direct Causation**:
```
Without pause:
  Thread A: [getdents] ──────→ [lock] ────→ [read] ────→ [unlock]
  Thread B:                [getdents] ──→ [lock] ───→ ...
  
  Result: Thread B waits normally for lock

With eBPF pause (DIRECTLY):
  Thread A: [getdents] ──PAUSE──→ [lock] ────→ [read] ────→ [unlock]
                         5ms delay!
  Thread B:                [getdents] ──→ [EAGAIN] (can't get lock)
  
  Result: Race window expanded RIGHT at critical moment!
```

**The pause happens EXACTLY where we want it**, at the moment of the syscall.

---

## Advantages

**1. Perfect timing**:
- Pause happens at the hook point (syscall entry)
- No latency - immediate effect
- Catches races at the exact critical moment

**2. Simple**:
- No userspace coordination needed
- No SIGSTOP/SIGCONT complexity
- Just eBPF program alone

**3. Jepsen-like**:
- Nemesis directly causes chaos
- Deterministic injection point
- Reproducible

**4. Efficient**:
- No context switches to userspace
- No signal overhead
- CPU-based delay (deterministic)

---

## Disadvantages (Why Busy-Wait?)

**Busy-waiting wastes CPU**, but:

✅ **For testing**: This is GOOD!
- We WANT to hold that CPU
- We WANT other threads to compete
- We WANT to expand the race window

✅ **Controllable**:
- Limit pauses to 1-10ms (short enough)
- Only pause small % of operations (5-20%)
- CPU usage spike is temporary and acceptable for testing

✅ **Realistic**:
- Simulates slow I/O or page faults
- Represents real-world delays
- Just compressed in time

---

## eBPF Limitations

**eBPF CANNOT**:
- ❌ Call sleep() or usleep()
- ❌ Call blocking functions
- ❌ Send signals
- ❌ Context switch

**eBPF CAN**:
- ✅ Busy-wait (what we use)
- ✅ Read time (bpf_ktime_get_ns)
- ✅ Use loops (with bounded iteration)
- ✅ Access maps

**Therefore**: Busy-wait is the ONLY way to pause directly in eBPF.

---

## Alternative Approach (What We Had)

**eBPF → Userspace → SIGSTOP approach**:

**Advantages**:
- No busy-waiting
- More efficient (process actually sleeps)

**Disadvantages**:
- ❌ Wrong timing (pause happens AFTER the critical moment)
- ❌ Complex (requires userspace coordinator)
- ❌ Latency (milliseconds to userspace and back)
- ❌ Not Jepsen-like (indirect chaos)

**Conclusion**: The userspace approach is wrong for chaos testing!

---

## The Right Design: Direct eBPF Busy-Wait

**Benefits for chaos testing**:

1. **Exact timing**: Pause RIGHT at the hook point
2. **Deterministic**: Always same delay (reproducible)
3. **Simple**: Just eBPF program, no userspace needed
4. **Effective**: Expands race windows exactly where we want

**Trade-off**: Burns CPU during pauses (acceptable for testing!)

---

## Userspace Controller Role (Revised)

**Instead of pausing processes**, userspace controller now:

1. **Loads eBPF program**
2. **Configures pause parameters** (probability, duration)
3. **Monitors statistics** (how many pauses injected)
4. **Adjusts dynamically** (increase pause rate if no bugs found)
5. **Reports results**

**It does NOT pause processes** - eBPF does that directly!

---

## Example: Finding f_pos Race

**Without eBPF**:
```
Thread A: read f_pos (0x1000)
          use f_pos     ← If Thread B runs here, RARE race
          write f_pos (0x2000)

Thread B: read f_pos  
          ...

Race window: ~100 nanoseconds (very rare!)
```

**With eBPF pause**:
```
Thread A: getdents64 syscall entry
          ← EBPF PAUSES HERE FOR 5ms
          read f_pos (0x1000)
          use f_pos     ← Thread B DEFINITELY runs during 5ms pause!
          write f_pos (0x2000)

Thread B: (runs during Thread A's pause)
          read f_pos (0x1000)  ← SAME VALUE!
          use f_pos            ← RACE!

Race window: 5 milliseconds (50,000x more likely!)
```

---

## Why This Matters

**Goal of chaos testing**: Make rare races common

**eBPF direct pause**:
- Expands race windows by 10,000x - 100,000x
- Makes "once in a million" bugs happen every few hundred iterations
- Deterministic (always pauses at same point)

**Result**: Find bugs in hours instead of months!

---

## Summary

**How eBPF causes pauses**:
1. Hook fires at critical moment (syscall entry, lock acquisition, etc.)
2. eBPF busy-waits for N microseconds **right there**
3. Execution held at that exact point
4. Other threads can now race during the pause
5. Race window expanded by orders of magnitude

**No userspace needed for pausing** - eBPF does it directly via busy-wait!

**Userspace role**: Configuration and monitoring only

---

*Next: Update pause_injector.bpf.c to use direct busy-wait approach*

