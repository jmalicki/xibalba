#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/**
 * RUDRA eBPF Pause Injector - Tech De-Risking PoC
 * 
 * Hooks getdents64 syscall and requests userspace to pause the process
 * Goal: Prove eBPF → userspace coordination works
 */

char LICENSE[] SEC("license") = "GPL";

// Perf event array for sending pause requests to userspace
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} pause_events SEC(".maps");

// Configuration map (pause probability percentage)
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} config SEC(".maps");

// Event structure sent to userspace
struct pause_request {
    __u32 pid;
    __u32 tid;
    __u64 timestamp_ns;
    __u32 cpu;
};

// Hook: getdents64 syscall entry
SEC("tracepoint/syscalls/sys_enter_getdents64")
int trace_getdents64_entry(void *ctx) {
    // Read pause probability from config map
    __u32 key = 0;
    __u32 *pause_prob = bpf_map_lookup_elem(&config, &key);
    if (!pause_prob || *pause_prob == 0)
        return 0;
    
    // Random decision: should we request a pause?
    __u32 rand = bpf_get_prandom_u32();
    if ((rand % 100) >= *pause_prob)
        return 0;  // Not this time
    
    // Create pause request
    struct pause_request req = {
        .pid = bpf_get_current_pid_tgid() >> 32,
        .tid = (__u32)bpf_get_current_pid_tgid(),
        .timestamp_ns = bpf_ktime_get_ns(),
        .cpu = bpf_get_smp_processor_id(),
    };
    
    // Send to userspace via perf buffer
    bpf_perf_event_output(ctx, &pause_events, BPF_F_CURRENT_CPU,
                          &req, sizeof(req));
    
    return 0;
}

