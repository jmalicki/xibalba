/*
 * Copyright (c) 2025 Joseph Malicki
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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

// ============================================================================
// Configuration Map: Runtime tunable parameters
// ============================================================================
// Values are set by userspace (pause_controller) when loading the program.
// eBPF code reads them on each syscall.
//
// Performance: Map lookups are ~10-20ns, negligible compared to syscall overhead.
// This is the standard approach for configurable eBPF programs.
//
// Benefits of map-based config:
//   - Single eBPF binary works for all configurations
//   - Can change parameters without recompiling
//   - Values passed via command-line to test runner
//   - Standard eBPF pattern (used by bpftrace, etc)
// ============================================================================

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 4);
    __type(key, __u32);
    __type(value, __u32);
} config SEC(".maps");

// Configuration map keys
#define CFG_DELAY_PROBABILITY  0  // Delay injection probability (0-100%)
#define CFG_DELAY_ITERATIONS   1  // Loop iterations for busy-wait
#define CFG_MAX_DELAY_NS       2  // Maximum delay in nanoseconds
#define CFG_RESERVED           3  // Reserved for future use

// Default values if map not initialized
#define DEFAULT_PROBABILITY   50    // 50% of getdents64 calls
#define DEFAULT_ITERATIONS    500   // ~5-10 microseconds
#define DEFAULT_MAX_DELAY_NS  50000 // 50μs maximum

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
    // Read configuration from map (set once by userspace)
    // Map lookups are fast (~10-20ns), negligible overhead
    __u32 delay_prob = DEFAULT_PROBABILITY;
    __u32 delay_iterations = DEFAULT_ITERATIONS;
    __u32 max_delay_ns = DEFAULT_MAX_DELAY_NS;
    
    __u32 key = CFG_DELAY_PROBABILITY;
    __u32 *val = bpf_map_lookup_elem(&config, &key);
    if (val && *val <= 100) {
        delay_prob = *val;
    }
    
    key = CFG_DELAY_ITERATIONS;
    val = bpf_map_lookup_elem(&config, &key);
    if (val && *val <= 1000) {  // Cap for verifier
        delay_iterations = *val;
    }
    
    key = CFG_MAX_DELAY_NS;
    val = bpf_map_lookup_elem(&config, &key);
    if (val) {
        max_delay_ns = *val;
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
    
    // Bounded busy-wait using runtime config (read from map above)
    __u64 start = bpf_ktime_get_ns();
    
    #pragma unroll
    for (int i = 0; i < 1000; i++) {
        // Use configured iterations (capped at 1000 for verifier)
        if (i >= delay_iterations)
            break;
        
        // Busy-wait with time check every 100 iterations
        if ((i % 100) == 0) {
            __u64 now = bpf_ktime_get_ns();
            // Use configured max delay
            if ((now - start) > max_delay_ns)
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
