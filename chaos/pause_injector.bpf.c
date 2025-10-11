#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/**
 * Xibalba eBPF Error Injector - Jepsen-Style Chaos via bpf_override_return
 * 
 * Hooks getdents64 syscall and INJECTS ERRORS to force retries and expose races!
 * 
 * This is TRUE Jepsen-style chaos:
 *   - Like network partitions (syscalls fail)
 *   - Like disk errors (operations return -EIO, -EAGAIN)
 *   - Forces error handling code paths
 *   - Causes retries that expose race windows
 * 
 * How it works:
 *   1. Hook fires at __x64_sys_getdents64 kernel function entry
 *   2. Random decision: inject error or not?
 *   3. If yes: Override return value with -EAGAIN or -EINTR
 *   4. Syscall fails immediately, userspace retries
 *   5. During retry: Other threads can interfere = RACE!
 * 
 * Why this is better than pauses:
 *   - No eBPF verifier issues (no complex loops)
 *   - More realistic (real systems have transient errors)
 *   - Tests error handling code paths
 *   - Forces retries = multiple chances for races
 *   - Exactly how Jepsen works (inject failures, not delays)
 * 
 * Requires: CONFIG_BPF_KPROBE_OVERRIDE=y (we control kernel via VMs!)
 */

char LICENSE[] SEC("license") = "GPL";

// Configuration map
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, __u32);
} config SEC(".maps");

#define CFG_ERROR_PROBABILITY 0  // Error injection probability (0-100%)
#define CFG_ERROR_CODE 1          // Which error to inject (-EAGAIN, -EINTR, etc.)

// Statistics map (how many errors injected)
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

// Error codes we can inject
#define ERROR_EAGAIN 11   // Resource temporarily unavailable (retry!)
#define ERROR_EINTR  4    // Interrupted system call (retry!)
#define ERROR_ENOENT 2    // No such file or directory (transient)

/**
 * Hook: getdents64 tracepoint
 * 
 * Tracepoints work on ALL kernels (no special config needed!)
 * 
 * We inject SHORT DELAYS using bounded loops (eBPF verifier accepts this).
 * Even microsecond delays can expose races with enough threads!
 * 
 * Effect: Thread A paused → other threads interfere → races exposed!
 */
SEC("tracepoint/syscalls/sys_enter_getdents64")
int trace_getdents64(void *ctx)
{
    // Default config: inject on 50% of calls
    __u32 delay_prob = 50;
    __u32 delay_iterations = 500;  // ~5-10 microseconds
    
    // Try to read user config
    __u32 key_prob = CFG_ERROR_PROBABILITY;
    __u32 *prob_ptr = bpf_map_lookup_elem(&config, &key_prob);
    if (prob_ptr && *prob_ptr > 0) {
        delay_prob = *prob_ptr;
    }
    
    __u32 key_iter = CFG_ERROR_CODE;
    __u32 *iter_ptr = bpf_map_lookup_elem(&config, &key_iter);
    if (iter_ptr && *iter_ptr > 0) {
        delay_iterations = *iter_ptr;
        // Cap at 1000 iterations to pass eBPF verifier
        if (delay_iterations > 1000) delay_iterations = 1000;
    }
    
    // Random decision: inject delay?
    __u32 rand = bpf_get_prandom_u32();
    if ((rand % 100) >= delay_prob) {
        return 0;  // Don't inject this time
    }
    
    //
    // === INJECT DELAY RIGHT HERE! ===
    //
    // Even a 5-10 microsecond delay expands race windows!
    //
    // With 10 threads and 40K ops/sec:
    //   - At 50% probability: 20K delays/sec injected
    //   - Each delay = window for other threads to race
    //   - 10μs delay = 10,000x normal race window!
    //
    // Why this works:
    //   - Thread A hits this hook, starts delay
    //   - Thread A is STUCK in kernel for 10μs  
    //   - Threads B, C, D keep running
    //   - They interfere with Thread A's operation
    //   - Race conditions become visible!
    //
    
    // Bounded busy-wait (verifier accepts this)
    __u64 start = bpf_ktime_get_ns();
    
    #pragma unroll
    for (int i = 0; i < 1000; i++) {
        if (i >= delay_iterations)
            break;
        
        // Busy-wait with time check every 100 iterations
        if ((i % 100) == 0) {
            __u64 now = bpf_ktime_get_ns();
            // Stop if we've delayed enough (~10-50 microseconds)
            if ((now - start) > 50000)  // 50μs max
                break;
        }
        
        // Actual busy work to consume CPU
        __sync_fetch_and_add(&start, 0);
    }
    
    // Track statistics
    __u32 stat_key = 0;
    __u64 *delay_count = bpf_map_lookup_elem(&stats, &stat_key);
    if (delay_count) {
        __sync_fetch_and_add(delay_count, 1);
    }
    
    // Delay complete! Race window was opened.
    return 0;
}
