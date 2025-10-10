#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/**
 * RUDRA eBPF Pause Injector - Direct Pause via Busy-Wait
 * 
 * Hooks getdents64 syscall and DIRECTLY pauses execution via busy-wait
 * 
 * This is Jepsen-style: eBPF directly causes chaos at the exact moment!
 * No userspace coordination needed - pause happens immediately.
 * 
 * How it works:
 *   1. Hook fires (getdents64 syscall entry)
 *   2. Random decision: pause or not?
 *   3. If pause: busy-wait for N microseconds RIGHT HERE
 *   4. Continue execution
 * 
 * This expands race windows by 10,000x - 100,000x!
 */

char LICENSE[] SEC("license") = "GPL";

// Configuration map
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, __u32);
} config SEC(".maps");

#define CFG_PAUSE_PROBABILITY 0  // Pause probability (0-100%)
#define CFG_PAUSE_DURATION_US 1  // Pause duration (microseconds)

// Statistics map (how many pauses injected)
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

/**
 * bpf_pause() - Directly pause execution via busy-wait
 * 
 * @delay_us: Delay in microseconds
 * 
 * This creates a pause RIGHT at the hook point by busy-waiting.
 * Burns CPU, but that's acceptable for testing - we WANT to hold
 * this execution path and let other threads race!
 * 
 * Why busy-wait?
 *   - eBPF can't call sleep() or usleep()
 *   - eBPF can't block
 *   - eBPF can't context switch
 *   - But eBPF CAN read time and loop!
 * 
 * Effect: Expands race windows dramatically
 */
static __always_inline void bpf_pause(__u32 delay_us) {
    if (delay_us == 0 || delay_us > 100000)  // Max 100ms safety limit
        return;
    
    __u64 start_ns = bpf_ktime_get_ns();
    __u64 end_ns = start_ns + ((__u64)delay_us * 1000);  // Convert to nanoseconds
    
    // Busy-wait loop
    // This holds execution at THIS EXACT POINT
    // Other threads can now race during this window
    __u64 now;
    for (int i = 0; i < 1000000; i++) {  // Safety limit on iterations
        now = bpf_ktime_get_ns();
        if (now >= end_ns)
            break;
    }
}

/**
 * Hook: getdents64 syscall entry
 * 
 * This is called EVERY TIME a process calls getdents64().
 * We randomly decide to pause, then IMMEDIATELY pause right here.
 * 
 * Effect: If Thread A is paused here, Thread B can run and cause races!
 */
SEC("tracepoint/syscalls/sys_enter_getdents64")
int trace_getdents64_entry(void *ctx) {
    // Read pause probability from config map
    __u32 key_prob = CFG_PAUSE_PROBABILITY;
    __u32 *pause_prob = bpf_map_lookup_elem(&config, &key_prob);
    if (!pause_prob || *pause_prob == 0)
        return 0;  // Pausing disabled
    
    // Read pause duration from config map
    __u32 key_dur = CFG_PAUSE_DURATION_US;
    __u32 *pause_duration = bpf_map_lookup_elem(&config, &key_dur);
    if (!pause_duration || *pause_duration == 0)
        return 0;  // No duration set
    
    // Random decision: should we pause THIS call?
    __u32 rand = bpf_get_prandom_u32();
    if ((rand % 100) >= *pause_prob)
        return 0;  // Not this time (e.g., 80% of the time if prob=20%)
    
    //
    // PAUSE RIGHT HERE!
    //
    // This is the magic moment:
    // - We're AT the getdents64 syscall entry
    // - About to acquire locks, read directory, etc.
    // - If we pause NOW, other threads can interfere
    // - Race conditions become MUCH more likely!
    //
    bpf_pause(*pause_duration);
    
    // Update statistics
    __u32 stat_key = 0;
    __u64 *pause_count = bpf_map_lookup_elem(&stats, &stat_key);
    if (pause_count) {
        __sync_fetch_and_add(pause_count, 1);
    }
    
    // Continue execution normally
    // The pause already happened - we expanded the race window!
    return 0;
}
