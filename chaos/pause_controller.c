#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * RUDRA Pause Controller - Simplified Version
 * 
 * This program ONLY:
 *   1. Loads the eBPF program
 *   2. Configures pause parameters
 *   3. Monitors statistics
 * 
 * The eBPF program itself does the pausing via busy-wait!
 * No more userspace pause handling - much simpler and more effective.
 */

static volatile bool keep_running = true;

void signal_handler(int sig) {
    (void)sig;
    keep_running = false;
}

int main(int argc, char *argv[]) {
    struct bpf_object *obj;
    int err;
    
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <pause_probability_pct> <pause_duration_us>\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s 20 5000   # 20%% pause probability, 5ms pauses\n", argv[0]);
        fprintf(stderr, "  %s 50 1000   # 50%% probability, 1ms pauses\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "The eBPF program will DIRECTLY pause processes at critical moments.\n");
        fprintf(stderr, "This expands race windows and makes bugs much more likely!\n");
        return 1;
    }
    
    uint32_t pause_prob = atoi(argv[1]);
    uint32_t pause_duration_us = atoi(argv[2]);
    
    if (pause_prob > 100) {
        fprintf(stderr, "ERROR: Pause probability must be 0-100%%\n");
        return 1;
    }
    
    if (pause_duration_us > 100000) {
        fprintf(stderr, "ERROR: Pause duration too high (max 100ms = 100000us)\n");
        return 1;
    }
    
    printf("=== RUDRA Pause Controller ===\n");
    printf("Pause probability: %u%%\n", pause_prob);
    printf("Pause duration: %uμs\n", pause_duration_us);
    printf("\n");
    printf("eBPF will DIRECTLY pause execution via busy-wait.\n");
    printf("This happens at the exact moment of getdents64 syscall!\n");
    printf("\n");
    
    // Setup signal handler
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
    struct bpf_link *link = NULL;
    
    bpf_object__for_each_program(prog, obj) {
        link = bpf_program__attach(prog);
        if (link) {
            printf("  ✓ Attached: %s\n", bpf_program__name(prog));
        } else {
            fprintf(stderr, "  ✗ Failed to attach: %s\n", bpf_program__name(prog));
            bpf_object__close(obj);
            return 1;
        }
    }
    
    // Configure pause parameters
    printf("Setting configuration...\n");
    int config_fd = bpf_object__find_map_fd_by_name(obj, "config");
    if (config_fd < 0) {
        fprintf(stderr, "ERROR: Can't find config map\n");
        bpf_object__close(obj);
        return 1;
    }
    
    // Set pause probability
    uint32_t key = 0;  // CFG_PAUSE_PROBABILITY
    err = bpf_map_update_elem(config_fd, &key, &pause_prob, BPF_ANY);
    if (err) {
        fprintf(stderr, "ERROR: Failed to set pause probability: %d\n", err);
        bpf_object__close(obj);
        return 1;
    }
    
    // Set pause duration
    key = 1;  // CFG_PAUSE_DURATION_US
    err = bpf_map_update_elem(config_fd, &key, &pause_duration_us, BPF_ANY);
    if (err) {
        fprintf(stderr, "ERROR: Failed to set pause duration: %d\n", err);
        bpf_object__close(obj);
        return 1;
    }
    
    printf("  ✓ Pause probability: %u%%\n", pause_prob);
    printf("  ✓ Pause duration: %uμs\n", pause_duration_us);
    
    // Initialize statistics
    int stats_fd = bpf_object__find_map_fd_by_name(obj, "stats");
    if (stats_fd >= 0) {
        key = 0;
        uint64_t zero = 0;
        bpf_map_update_elem(stats_fd, &key, &zero, BPF_ANY);
    }
    
    printf("\n");
    printf("🚀 Pause injector active!\n");
    printf("   eBPF is now intercepting getdents64 syscalls\n");
    printf("   %u%% of calls will be paused for %uμs via busy-wait\n",
           pause_prob, pause_duration_us);
    printf("   This expands race windows by ~%ux\n", pause_duration_us / 100);
    printf("\n");
    printf("Press Ctrl+C to stop and see statistics\n");
    printf("\n");
    
    // Monitor statistics periodically
    uint64_t last_count = 0;
    while (keep_running) {
        sleep(2);
        
        if (stats_fd >= 0) {
            key = 0;
            uint64_t count = 0;
            if (bpf_map_lookup_elem(stats_fd, &key, &count) == 0) {
                if (count != last_count) {
                    printf("Pauses injected: %lu (+%lu in last 2s)\n", 
                           count, count - last_count);
                    last_count = count;
                }
            }
        }
    }
    
    printf("\n=== Shutdown ===\n");
    if (stats_fd >= 0) {
        key = 0;
        uint64_t count = 0;
        if (bpf_map_lookup_elem(stats_fd, &key, &count) == 0) {
            printf("Total pauses injected: %lu\n", count);
        }
    }
    
    if (link)
        bpf_link__destroy(link);
    bpf_object__close(obj);
    
    return 0;
}
