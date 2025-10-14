/*
 * Xibalba eBPF Injector: Rename Tracepoint
 * 
 * Injects delays at rename/unlink syscall exits to expose race conditions.
 * Uses stable tracepoint ABI (works across kernel versions).
 * 
 * Targets:
 * - Missing files during rename (file invisible window)
 * - Lost rename operations
 * - Directory consistency violations
 * 
 * Based on: docs/design/BTRFS-LOST-UPDATE-ANALYSIS.md
 * Approach: Tracepoint (syscall level) for portability
 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

// ============================================================================
// Configuration Maps
// ============================================================================

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 3);
    __type(key, __u32);
    __type(value, __u32);
} config SEC(".maps");

#define CFG_PROBABILITY 0  // Probability of injecting delay (0-100)
#define CFG_DELAY_US 1     // Delay in microseconds
#define CFG_ENABLED 2      // Enable/disable injection

// Statistics Maps
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 6);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

#define STAT_TOTAL_CALLS 0
#define STAT_DELAYS_INJECTED 1
#define STAT_RENAME_CALLS 2
#define STAT_UNLINK_CALLS 3
#define STAT_TOTAL_DELAY_NS 4
#define STAT_SKIPPED 5  // Calls where we didn't inject

// ============================================================================
// Helper: Should Inject Delay?
// ============================================================================

static __always_inline int should_inject(void) {
    __u32 key_enabled = CFG_ENABLED;
    __u32 *enabled = bpf_map_lookup_elem(&config, &key_enabled);
    if (!enabled || *enabled == 0)
        return 0;
    
    __u32 key_prob = CFG_PROBABILITY;
    __u32 *probability = bpf_map_lookup_elem(&config, &key_prob);
    if (!probability || *probability == 0)
        return 0;
    
    __u32 rand = bpf_get_prandom_u32();
    return (rand % 100) < *probability;
}

static __always_inline void inject_delay(void) {
    __u32 key_delay = CFG_DELAY_US;
    __u32 *delay_us = bpf_map_lookup_elem(&config, &key_delay);
    if (!delay_us || *delay_us == 0)
        return;
    
    // Busy-wait for specified duration
    __u64 start_ns = bpf_ktime_get_ns();
    __u64 end_ns = start_ns + ((__u64)(*delay_us) * 1000);
    
    // Bounded loop to prevent verifier issues
    #pragma unroll
    for (int i = 0; i < 1000; i++) {
        if (bpf_ktime_get_ns() >= end_ns)
            break;
    }
    
    // Update delay stat
    __u32 stat_key = STAT_TOTAL_DELAY_NS;
    __u64 *delay_ns = bpf_map_lookup_elem(&stats, &stat_key);
    if (delay_ns) {
        __u64 actual_delay = bpf_ktime_get_ns() - start_ns;
        __sync_fetch_and_add(delay_ns, actual_delay);
    }
}

// ============================================================================
// Tracepoint Context Structures
// ============================================================================

// Syscall exit tracepoint context (from kernel tracepoint definition)
struct trace_event_raw_sys_exit {
    __u64 unused;
    long id;      // syscall number
    long ret;     // return value
};

// ============================================================================
// Hook 1: sys_exit_renameat2 - Delay After Successful Rename
// ============================================================================

SEC("tracepoint/syscalls/sys_exit_renameat2")
int trace_rename_exit(struct trace_event_raw_sys_exit *ctx) {
    // Update call count
    __u32 stat_key = STAT_RENAME_CALLS;
    __u64 *count = bpf_map_lookup_elem(&stats, &stat_key);
    if (count)
        __sync_fetch_and_add(count, 1);
    
    // Only delay if rename succeeded
    if (ctx->ret != 0) {
        // Failed rename - don't inject
        __u32 skip_key = STAT_SKIPPED;
        __u64 *skipped = bpf_map_lookup_elem(&stats, &skip_key);
        if (skipped)
            __sync_fetch_and_add(skipped, 1);
        return 0;
    }
    
    // At this point:
    // - Rename syscall completed successfully
    // - File MAY still be visible at both old and new paths briefly
    // - OR file MAY be invisible at both paths briefly
    // - Delaying here widens that window for directory readers
    
    if (!should_inject()) {
        __u32 skip_key = STAT_SKIPPED;
        __u64 *skipped = bpf_map_lookup_elem(&stats, &skip_key);
        if (skipped)
            __sync_fetch_and_add(skipped, 1);
        return 0;
    }
    
    inject_delay();
    
    // Update total call count and injection count
    __u32 total_key = STAT_TOTAL_CALLS;
    __u64 *total = bpf_map_lookup_elem(&stats, &total_key);
    if (total)
        __sync_fetch_and_add(total, 1);
    
    __u32 inj_key = STAT_DELAYS_INJECTED;
    __u64 *inj_count = bpf_map_lookup_elem(&stats, &inj_key);
    if (inj_count)
        __sync_fetch_and_add(inj_count, 1);
    
    return 0;
}

// ============================================================================
// Hook 2: sys_exit_unlinkat - Delay After Successful Unlink
// ============================================================================

SEC("tracepoint/syscalls/sys_exit_unlinkat")
int trace_unlink_exit(struct trace_event_raw_sys_exit *ctx) {
    // Update call count
    __u32 stat_key = STAT_UNLINK_CALLS;
    __u64 *count = bpf_map_lookup_elem(&stats, &stat_key);
    if (count)
        __sync_fetch_and_add(count, 1);
    
    // Only delay if unlink succeeded
    if (ctx->ret != 0) {
        __u32 skip_key = STAT_SKIPPED;
        __u64 *skipped = bpf_map_lookup_elem(&stats, &skip_key);
        if (skipped)
            __sync_fetch_and_add(skipped, 1);
        return 0;
    }
    
    // Unlink succeeded - might be part of rename operation
    // Delaying here can expose the window where file is removed
    // but not yet added to new location
    
    if (!should_inject()) {
        __u32 skip_key = STAT_SKIPPED;
        __u64 *skipped = bpf_map_lookup_elem(&stats, &skip_key);
        if (skipped)
            __sync_fetch_and_add(skipped, 1);
        return 0;
    }
    
    inject_delay();
    
    __u32 total_key = STAT_TOTAL_CALLS;
    __u64 *total = bpf_map_lookup_elem(&stats, &total_key);
    if (total)
        __sync_fetch_and_add(total, 1);
    
    __u32 inj_key = STAT_DELAYS_INJECTED;
    __u64 *inj_count = bpf_map_lookup_elem(&stats, &inj_key);
    if (inj_count)
        __sync_fetch_and_add(inj_count, 1);
    
    return 0;
}

