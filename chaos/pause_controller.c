#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

/**
 * RUDRA Pause Controller - Tech De-Risking PoC
 * 
 * Receives pause requests from eBPF and pauses processes using SIGSTOP/SIGCONT
 * Goal: Prove eBPF → userspace → process pause coordination works
 */

struct pause_request {
    uint32_t pid;
    uint32_t tid;
    uint64_t timestamp_ns;
    uint32_t cpu;
};

static volatile bool keep_running = true;
static uint64_t pauses_injected = 0;
static uint32_t pause_duration_us = 5000;  // 5ms default

void signal_handler(int sig) {
    keep_running = false;
}

void handle_pause_event(void *ctx, int cpu, void *data, __u32 size) {
    struct pause_request *req = data;
    
    printf("[CPU %d] Pause request: pid=%d, tid=%d, ts=%lu\n",
           cpu, req->pid, req->tid, (unsigned long)req->timestamp_ns);
    
    // Pause the process using SIGSTOP
    if (kill(req->pid, SIGSTOP) == 0) {
        // Process stopped successfully
        usleep(pause_duration_us);
        
        // Resume with SIGCONT
        if (kill(req->pid, SIGCONT) == 0) {
            pauses_injected++;
            printf("  ✓ Paused pid=%d for %uμs (total: %lu)\n",
                   req->pid, pause_duration_us, pauses_injected);
        } else {
            perror("  ✗ Failed to resume (SIGCONT)");
        }
    } else {
        // Process might have exited, that's OK
        if (errno != ESRCH) {  // ESRCH = no such process
            perror("  ✗ Failed to pause (SIGSTOP)");
        }
    }
}

int main(int argc, char *argv[]) {
    struct bpf_object *obj;
    struct perf_buffer *pb;
    int err;
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pause_probability_pct> [pause_duration_us]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s 20       # 20%% pause probability, 5ms pauses\n", argv[0]);
        fprintf(stderr, "  %s 50 10000 # 50%% probability, 10ms pauses\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Run this BEFORE starting chaos test!\n");
        return 1;
    }
    
    int pause_prob = atoi(argv[1]);
    if (argc >= 3)
        pause_duration_us = atoi(argv[2]);
    
    printf("=== RUDRA Pause Controller ===\n");
    printf("Pause probability: %d%%\n", pause_prob);
    printf("Pause duration: %uμs\n", pause_duration_us);
    printf("\n");
    
    // Setup signal handler for clean shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Load eBPF program
    printf("Loading eBPF program...\n");
    obj = bpf_object__open_file("pause_injector.bpf.o", NULL);
    if (!obj) {
        fprintf(stderr, "ERROR: Failed to open eBPF object file\n");
        fprintf(stderr, "Make sure pause_injector.bpf.o is in current directory\n");
        return 1;
    }
    
    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "ERROR: Failed to load eBPF program: %d\n", err);
        fprintf(stderr, "Check 'dmesg' for kernel errors\n");
        bpf_object__close(obj);
        return 1;
    }
    
    printf("  ✓ eBPF program loaded\n");
    
    // Attach programs
    printf("Attaching eBPF hooks...\n");
    struct bpf_program *prog;
    struct bpf_link *link;
    int attached = 0;
    
    bpf_object__for_each_program(prog, obj) {
        link = bpf_program__attach(prog);
        if (link) {
            printf("  ✓ Attached: %s\n", bpf_program__name(prog));
            attached++;
        } else {
            fprintf(stderr, "  ✗ Failed to attach: %s\n", bpf_program__name(prog));
        }
    }
    
    if (attached == 0) {
        fprintf(stderr, "ERROR: No programs attached\n");
        bpf_object__close(obj);
        return 1;
    }
    
    // Set configuration (pause probability)
    int config_fd = bpf_object__find_map_fd_by_name(obj, "config");
    if (config_fd < 0) {
        fprintf(stderr, "ERROR: Can't find config map\n");
        bpf_object__close(obj);
        return 1;
    }
    
    uint32_t key = 0;
    uint32_t val = pause_prob;
    err = bpf_map_update_elem(config_fd, &key, &val, BPF_ANY);
    if (err) {
        fprintf(stderr, "ERROR: Failed to update config: %d\n", err);
        bpf_object__close(obj);
        return 1;
    }
    
    printf("  ✓ Configuration set: %d%% pause probability\n", pause_prob);
    
    // Setup perf buffer for receiving pause events
    int events_fd = bpf_object__find_map_fd_by_name(obj, "pause_events");
    if (events_fd < 0) {
        fprintf(stderr, "ERROR: Can't find pause_events map\n");
        bpf_object__close(obj);
        return 1;
    }
    
    pb = perf_buffer__new(events_fd, 8, handle_pause_event, NULL, NULL, NULL);
    if (!pb) {
        fprintf(stderr, "ERROR: Failed to create perf buffer\n");
        bpf_object__close(obj);
        return 1;
    }
    
    printf("  ✓ Perf buffer created\n");
    printf("\n");
    printf("🚀 Pause controller active!\n");
    printf("   Monitoring getdents64 syscalls...\n");
    printf("   Will pause %d%% of calls for %uμs each\n", pause_prob, pause_duration_us);
    printf("\n");
    printf("Press Ctrl+C to stop\n");
    printf("\n");
    
    // Poll for events
    while (keep_running) {
        err = perf_buffer__poll(pb, 100);  // 100ms timeout
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "Error polling perf buffer: %d\n", err);
            break;
        }
    }
    
    printf("\n=== Shutdown ===\n");
    printf("Total pauses injected: %lu\n", pauses_injected);
    
    perf_buffer__free(pb);
    bpf_object__close(obj);
    
    return 0;
}

