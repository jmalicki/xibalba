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

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Xibalba Delay Injector Controller
 * 
 * Controls eBPF delay injection for chaos testing.
 * 
 * This program:
 *   1. Loads the eBPF program
 *   2. Configures delay probability, iterations, and max delay
 *   3. Monitors how many delays are injected
 * 
 * The eBPF program injects microsecond-scale delays in getdents64
 * to widen race windows and expose concurrency bugs.
 */

static volatile bool keep_running = true;

static void signal_handler(int sig) {
    (void)sig;
    keep_running = false;
}

int main(int argc, char *argv[]) {
    struct bpf_object *obj;
    int err;
    
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <delay_probability_pct> <delay_iterations> [max_delay_ns]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Xibalba Delay Injector - Widen Race Windows via eBPF\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Parameters:\n");
        fprintf(stderr, "  delay_probability_pct  Percent of getdents64 calls to delay (0-100)\n");
        fprintf(stderr, "  delay_iterations       Loop iterations for busy-wait (1-1000)\n");
        fprintf(stderr, "  max_delay_ns           Maximum delay in nanoseconds (optional, default 50000)\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s 50 500         # 50%% probability, 500 iterations (~5-10μs)\n", argv[0]);
        fprintf(stderr, "  %s 30 1000 100000 # 30%% probability, 1000 iterations, 100μs max\n", argv[0]);
        fprintf(stderr, "  %s 100 200        # 100%% probability, minimal delay (aggressive)\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "The eBPF program injects delays to expand race windows.\n");
        fprintf(stderr, "Even 5-10μs delays make bugs 10,000x more likely to appear!\n");
        return 1;
    }
    
    uint32_t delay_prob = (uint32_t)atoi(argv[1]);
    uint32_t delay_iterations = (uint32_t)atoi(argv[2]);
    uint32_t max_delay_ns = 50000;  // Default 50μs
    
    if (argc >= 4) {
        max_delay_ns = (uint32_t)atoi(argv[3]);
    }
    
    if (delay_prob > 100) {
        fprintf(stderr, "ERROR: Delay probability must be 0-100%%\n");
        return 1;
    }
    
    if (delay_iterations > 1000) {
        fprintf(stderr, "ERROR: Delay iterations must be 1-1000 (eBPF verifier limit)\n");
        return 1;
    }
    
    if (max_delay_ns < 1000 || max_delay_ns > 1000000) {
        fprintf(stderr, "WARNING: max_delay_ns should be 1000-1000000 (1μs-1ms)\n");
    }
    
    printf("=== Xibalba Delay Injector ===\n");
    printf("Configuration:\n");
    printf("  Delay probability: %u%%\n", delay_prob);
    printf("  Delay iterations: %u (~%.1fμs)\n", delay_iterations, delay_iterations * 0.01);
    printf("  Max delay: %u ns (%.1fμs)\n", max_delay_ns, max_delay_ns / 1000.0);
    printf("\n");
    printf("eBPF will inject delays to widen race windows.\n");
    printf("Expected delays/sec: ~%u (if 40K ops/sec baseline)\n", 
           delay_prob * 400);
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
        fprintf(stderr, "\n");
        fprintf(stderr, "This program requires CONFIG_BPF_KPROBE_OVERRIDE=y\n");
        fprintf(stderr, "Check kernel config: zgrep CONFIG_BPF_KPROBE_OVERRIDE /proc/config.gz\n");
        fprintf(stderr, "Or check dmesg for more errors\n");
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
            fprintf(stderr, "\n");
            fprintf(stderr, "Kprobe attachment requires CONFIG_KPROBES=y\n");
            fprintf(stderr, "And may require sudo or CAP_BPF capability\n");
            bpf_object__close(obj);
            return 1;
        }
    }
    
    // Configure delay parameters
    printf("Setting configuration...\n");
    int config_fd = bpf_object__find_map_fd_by_name(obj, "config");
    if (config_fd < 0) {
        fprintf(stderr, "ERROR: Can't find config map\n");
        bpf_object__close(obj);
        return 1;
    }
    
    // Configuration keys (must match pause_injector.bpf.c)
    #define CFG_DELAY_PROBABILITY  0
    #define CFG_DELAY_ITERATIONS   1
    #define CFG_MAX_DELAY_NS       2
    
    // Set delay probability
    uint32_t key = CFG_DELAY_PROBABILITY;
    err = bpf_map_update_elem(config_fd, &key, &delay_prob, BPF_ANY);
    if (err) {
        fprintf(stderr, "ERROR: Failed to set delay probability: %d\n", err);
        bpf_object__close(obj);
        return 1;
    }
    
    // Set delay iterations
    key = CFG_DELAY_ITERATIONS;
    err = bpf_map_update_elem(config_fd, &key, &delay_iterations, BPF_ANY);
    if (err) {
        fprintf(stderr, "ERROR: Failed to set delay iterations: %d\n", err);
        bpf_object__close(obj);
        return 1;
    }
    
    // Set max delay
    key = CFG_MAX_DELAY_NS;
    err = bpf_map_update_elem(config_fd, &key, &max_delay_ns, BPF_ANY);
    if (err) {
        fprintf(stderr, "ERROR: Failed to set max delay: %d\n", err);
        bpf_object__close(obj);
        return 1;
    }
    
    printf("  ✓ Delay probability: %u%%\n", delay_prob);
    printf("  ✓ Delay iterations: %u\n", delay_iterations);
    printf("  ✓ Max delay: %u ns\n", max_delay_ns);
    
    // Initialize statistics
    int stats_fd = bpf_object__find_map_fd_by_name(obj, "stats");
    if (stats_fd >= 0) {
        key = 0;
        uint64_t zero = 0;
        bpf_map_update_elem(stats_fd, &key, &zero, BPF_ANY);
    }
    
    printf("\n");
    printf("🌩️  DELAY INJECTOR ACTIVE!\n");
    printf("   eBPF is now intercepting getdents64 syscalls\n");
    printf("   %u%% of calls will be delayed by ~%.1fμs (max %.1fμs)\n", 
           delay_prob, delay_iterations * 0.01, max_delay_ns / 1000.0);
    printf("   This widens race windows and exposes concurrency bugs!\n");
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
                    uint64_t delta = count - last_count;
                    printf("Delays injected: %lu (+%lu in last 2s = ~%lu/sec)\n", 
                           count, delta, delta / 2);
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
            printf("Total delays injected: %lu\n", count);
        }
    }
    
    if (link)
        bpf_link__destroy(link);
    bpf_object__close(obj);
    
    return 0;
}
