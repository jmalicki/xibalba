/*
 * Xibalba Unified Chaos Test Runner
 * 
 * Single command to run any workload with any eBPF injector.
 * Combines workload modules + eBPF controllers for systematic testing.
 * 
 * Usage:
 *   chaos_test_runner --workload rename --injector rename_tracepoint /tmp/test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include "../workloads/workload.h"
#include "../injectors/injector.h"
#include "../controllers/controller.h"
#include "../../common/state_tracker.h"

// Default configuration
#define DEFAULT_DURATION 60
#define MAX_READERS 50
#define MAX_WRITERS 50

// Global state for signal handling
static workload_state_t *g_state = NULL;

// Signal handler for clean shutdown
static void signal_handler(int signum) {
    (void)signum;
    if (g_state) {
        atomic_store(&g_state->stop, true);
    }
}

// Print usage
static void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS] <directory>\n", prog_name);
    printf("\n");
    printf("Xibalba Unified Chaos Test Runner\n");
    printf("Run filesystem chaos tests with pluggable workloads and eBPF injectors.\n");
    printf("\n");
    printf("Required:\n");
    printf("  <directory>          Test directory path\n");
    printf("\n");
    printf("Workload Options:\n");
    printf("  --workload NAME      Workload module (default: create_delete)\n");
    printf("  --list-workloads     List available workloads and exit\n");
    printf("\n");
    printf("Injector Options:\n");
    printf("  --injector NAME      eBPF injector (default: none)\n");
    printf("  --list-injectors     List available injectors and exit\n");
    printf("  --probability N      Injection probability %% (default: injector default)\n");
    printf("  --delay N            Injection delay μs (default: injector default)\n");
    printf("\n");
    printf("Test Configuration:\n");
    printf("  --filesystem FS      Filesystem type (default: auto)\n");
    printf("  --model MODEL        Consistency model: posix/weak/strict/eventual (default: posix)\n");
    printf("  --duration N         Test duration in seconds (default: 60)\n");
    printf("  --readers N          Number of reader threads (default: workload default)\n");
    printf("  --writers N          Number of writer threads (default: workload default)\n");
    printf("\n");
    printf("Output Options:\n");
    printf("  --json               Output results as JSON\n");
    printf("  --quiet              Minimal output\n");
    printf("\n");
    printf("Examples:\n");
    printf("  # List available options\n");
    printf("  %s --list-workloads\n", prog_name);
    printf("  %s --list-injectors\n", prog_name);
    printf("\n");
    printf("  # Run rename workload without injection (baseline)\n");
    printf("  %s --workload rename --duration 300 /tmp/test\n", prog_name);
    printf("\n");
    printf("  # Run rename workload with tracepoint injection\n");
    printf("  %s --workload rename --injector rename_tracepoint /tmp/test\n", prog_name);
    printf("\n");
    printf("  # Run hardlink workload with link injection (dirty read testing)\n");
    printf("  %s --workload hardlink --injector link_tracepoint --duration 300 /tmp/test\n", prog_name);
    printf("\n");
}

// Parse consistency model from string
static consistency_model_t parse_model(const char *str) {
    if (strcmp(str, "posix") == 0) return CONSISTENCY_POSIX;
    if (strcmp(str, "weak") == 0) return CONSISTENCY_WEAK_POSIX;
    if (strcmp(str, "strict") == 0) return CONSISTENCY_STRICT;
    if (strcmp(str, "eventual") == 0) return CONSISTENCY_EVENTUAL;
    
    fprintf(stderr, "Unknown consistency model: %s (using posix)\n", str);
    return CONSISTENCY_POSIX;
}

// Parse command-line arguments
typedef struct {
    const char *workload_name;
    const char *injector_name;
    const char *filesystem;
    const char *test_dir;
    consistency_model_t model;
    int duration;
    int readers;  // -1 = use workload default
    int writers;  // -1 = use workload default
    int probability;  // -1 = use injector default
    int delay;  // -1 = use injector default
    int json_output;
    int quiet;
    int list_workloads;
    int list_injectors;
} test_config_t;

static int parse_args(int argc, char *argv[], test_config_t *config) {
    // Defaults
    config->workload_name = "create_delete";
    config->injector_name = "none";
    config->filesystem = "auto";
    config->test_dir = NULL;
    config->model = CONSISTENCY_POSIX;
    config->duration = DEFAULT_DURATION;
    config->readers = -1;
    config->writers = -1;
    config->probability = -1;
    config->delay = -1;
    config->json_output = 0;
    config->quiet = 0;
    config->list_workloads = 0;
    config->list_injectors = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return -1;  // Show usage
        }
        else if (strcmp(argv[i], "--workload") == 0 && i + 1 < argc) {
            config->workload_name = argv[++i];
        }
        else if (strcmp(argv[i], "--injector") == 0 && i + 1 < argc) {
            config->injector_name = argv[++i];
        }
        else if (strcmp(argv[i], "--filesystem") == 0 && i + 1 < argc) {
            config->filesystem = argv[++i];
        }
        else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            config->model = parse_model(argv[++i]);
        }
        else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            config->duration = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--readers") == 0 && i + 1 < argc) {
            config->readers = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--writers") == 0 && i + 1 < argc) {
            config->writers = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--probability") == 0 && i + 1 < argc) {
            config->probability = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            config->delay = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--json") == 0) {
            config->json_output = 1;
        }
        else if (strcmp(argv[i], "--quiet") == 0) {
            config->quiet = 1;
        }
        else if (strcmp(argv[i], "--list-workloads") == 0) {
            config->list_workloads = 1;
        }
        else if (strcmp(argv[i], "--list-injectors") == 0) {
            config->list_injectors = 1;
        }
        else if (argv[i][0] != '-') {
            config->test_dir = argv[i];
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }
    
    return 0;
}

// Bug writer thread (handles async I/O)
static void *bug_writer_thread(void *arg) {
    workload_state_t *state = (workload_state_t *)arg;
    bug_queue_t *queue = (bug_queue_t *)state->bug_queue;
    
    uint64_t last_read_idx = 0;
    
    while (!atomic_load(&state->stop)) {
        uint64_t write_idx = atomic_load(&queue->write_idx);
        
        while (last_read_idx < write_idx) {
            uint64_t slot = last_read_idx % BUG_QUEUE_SIZE;
            bug_event_t *event = &queue->events[slot];
            (void)event;  // Reserved for future use
            
            // Just count for now - detailed logging happens in main thread
            last_read_idx++;
        }
        
        usleep(10000);  // 10ms
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    test_config_t config;
    
    if (argc < 2 || parse_args(argc, argv, &config) < 0) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Handle list commands
    if (config.list_workloads) {
        list_workloads(stdout);
        return 0;
    }
    
    if (config.list_injectors) {
        list_injectors(stdout);
        return 0;
    }
    
    // Validate test directory
    if (!config.test_dir) {
        fprintf(stderr, "Error: Test directory required\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    // Create test directory if needed
    mkdir(config.test_dir, 0755);
    
    // Load workload
    const workload_ops_t *workload = get_workload(config.workload_name);
    if (!workload) {
        fprintf(stderr, "Error: Unknown workload '%s'\n\n", config.workload_name);
        list_workloads(stderr);
        return 1;
    }
    
    // Determine thread counts
    int num_readers = (config.readers > 0) ? config.readers : workload->get_default_readers();
    int num_writers = (config.writers > 0) ? config.writers : workload->get_default_writers();
    
    if (!config.quiet) {
        printf("Xibalba Chaos Test Runner\n");
        printf("=========================\n\n");
        printf("Workload:     %s\n", workload->name);
        printf("Description:  %s\n", workload->description);
        printf("Directory:    %s\n", config.test_dir);
        printf("Duration:     %d seconds\n", config.duration);
        printf("Readers:      %d threads\n", num_readers);
        printf("Writers:      %d threads\n", num_writers);
        printf("Model:        %s\n", 
               config.model == CONSISTENCY_POSIX ? "posix" :
               config.model == CONSISTENCY_WEAK_POSIX ? "weak" :
               config.model == CONSISTENCY_STRICT ? "strict" : "eventual");
    }
    
    // Load injector (optional)
    controller_handle_t *injector_ctrl = NULL;
    const injector_descriptor_t *injector = NULL;
    
    if (strcmp(config.injector_name, "none") != 0) {
        injector = get_injector(config.injector_name);
        if (!injector) {
            fprintf(stderr, "\nError: Unknown injector '%s'\n\n", config.injector_name);
            list_injectors(stderr);
            return 1;
        }
        
        // Check filesystem compatibility
        if (!injector_supports_filesystem(injector, config.filesystem)) {
            fprintf(stderr, "\nWarning: Injector '%s' may not support filesystem '%s'\n",
                    config.injector_name, config.filesystem);
            fprintf(stderr, "Continuing anyway...\n\n");
        }
        
        // Build injector config
        injector_config_t inj_config = {
            .probability_pct = (config.probability > 0) ? 
                (uint32_t)config.probability : injector->config.default_probability_pct,
            .delay_us = (config.delay > 0) ? 
                (uint32_t)config.delay : injector->config.default_delay_us,
            .error_code = 0,
            .enabled = 1,
        };
        
        // Load injector
        char error_buf[256];
        injector_ctrl = controller_load_injector(injector, &inj_config, 
                                                  error_buf, sizeof(error_buf));
        if (!injector_ctrl) {
            fprintf(stderr, "\nError: Failed to load injector: %s\n", error_buf);
            fprintf(stderr, "Note: eBPF injection requires CAP_BPF or root privileges.\n");
            fprintf(stderr, "Continuing without injection...\n\n");
        } else if (!config.quiet) {
            printf("\neBPF Injector:\n");
            printf("  Name:         %s\n", injector->name);
            printf("  Description:  %s\n", injector->description);
            printf("  Probability:  %u%%\n", inj_config.probability_pct);
            printf("  Delay:        %u μs\n", inj_config.delay_us);
        }
    }
    
    if (!config.quiet) {
        printf("\nStarting test...\n\n");
    }
    
    // Initialize state tracker
    state_tracker_t *tracker = tracker_init();
    if (!tracker) {
        fprintf(stderr, "Failed to initialize state tracker\n");
        if (injector_ctrl) controller_unload(injector_ctrl);
        return 1;
    }
    
    // Create bug queue
    bug_queue_t *bug_queue = calloc(1, sizeof(bug_queue_t));
    if (!bug_queue) {
        fprintf(stderr, "Failed to allocate bug queue\n");
        tracker_cleanup(tracker);
        if (injector_ctrl) controller_unload(injector_ctrl);
        return 1;
    }
    
    // Initialize workload state
    workload_state_t state = {
        .test_dir = config.test_dir,
        .tracker = tracker,
        .model = config.model,
        .bug_queue = bug_queue,
        .scan_export = NULL,  // TODO: Add scan export file
        .workload_data = NULL,
        .ops = workload,
    };
    
    // Initialize atomics (can't use ATOMIC_VAR_INIT in C11)
    atomic_init(&state.stop, 0);
    atomic_init(&state.operations, 0);
    atomic_init(&state.bugs_found, 0);
    atomic_init(&state.reads_completed, 0);
    
    g_state = &state;
    
    // Initialize workload
    if (workload->init(&state, config.test_dir) < 0) {
        fprintf(stderr, "Failed to initialize workload\n");
        free(bug_queue);
        tracker_cleanup(tracker);
        if (injector_ctrl) controller_unload(injector_ctrl);
        return 1;
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Allocate thread arrays
    pthread_t *reader_threads = calloc((size_t)num_readers, sizeof(pthread_t));
    pthread_t *writer_threads = calloc((size_t)num_writers, sizeof(pthread_t));
    pthread_t bug_writer;
    
    if (!reader_threads || !writer_threads) {
        fprintf(stderr, "Failed to allocate thread arrays\n");
        workload->cleanup(&state);
        free(bug_queue);
        tracker_cleanup(tracker);
        if (injector_ctrl) controller_unload(injector_ctrl);
        return 1;
    }
    
    // Launch bug writer thread
    if (pthread_create(&bug_writer, NULL, bug_writer_thread, &state) != 0) {
        fprintf(stderr, "Failed to create bug writer thread\n");
        free(reader_threads);
        free(writer_threads);
        workload->cleanup(&state);
        free(bug_queue);
        tracker_cleanup(tracker);
        if (injector_ctrl) controller_unload(injector_ctrl);
        return 1;
    }
    
    // Launch reader threads
    for (int i = 0; i < num_readers; i++) {
        if (pthread_create(&reader_threads[i], NULL, workload->reader_fn, &state) != 0) {
            fprintf(stderr, "Failed to create reader thread %d\n", i);
            atomic_store(&state.stop, true);
            // Join already created threads
            for (int j = 0; j < i; j++) {
                pthread_join(reader_threads[j], NULL);
            }
            pthread_join(bug_writer, NULL);
            free(reader_threads);
            free(writer_threads);
            workload->cleanup(&state);
            free(bug_queue);
            tracker_cleanup(tracker);
            if (injector_ctrl) controller_unload(injector_ctrl);
            return 1;
        }
    }
    
    // Launch writer threads
    for (int i = 0; i < num_writers; i++) {
        if (pthread_create(&writer_threads[i], NULL, workload->writer_fn, &state) != 0) {
            fprintf(stderr, "Failed to create writer thread %d\n", i);
            atomic_store(&state.stop, true);
            // Join all threads
            for (int j = 0; j < num_readers; j++) {
                pthread_join(reader_threads[j], NULL);
            }
            for (int j = 0; j < i; j++) {
                pthread_join(writer_threads[j], NULL);
            }
            pthread_join(bug_writer, NULL);
            free(reader_threads);
            free(writer_threads);
            workload->cleanup(&state);
            free(bug_queue);
            tracker_cleanup(tracker);
            if (injector_ctrl) controller_unload(injector_ctrl);
            return 1;
        }
    }
    
    // Run test for specified duration
    if (!config.quiet) {
        printf("Running...\n");
    }
    
    sleep((unsigned int)config.duration);
    
    // Stop all threads
    atomic_store(&state.stop, true);
    
    // Join all threads
    for (int i = 0; i < num_readers; i++) {
        pthread_join(reader_threads[i], NULL);
    }
    
    for (int i = 0; i < num_writers; i++) {
        pthread_join(writer_threads[i], NULL);
    }
    
    pthread_join(bug_writer, NULL);
    
    // Print results
    if (config.json_output) {
        // JSON output
        printf("{\n");
        printf("  \"workload\": \"%s\",\n", workload->name);
        printf("  \"injector\": \"%s\",\n", config.injector_name);
        printf("  \"duration\": %d,\n", config.duration);
        printf("  \"operations\": %lu,\n", (unsigned long)atomic_load(&state.operations));
        printf("  \"bugs_found\": %lu,\n", (unsigned long)atomic_load(&state.bugs_found));
        printf("  \"reads_completed\": %lu\n", (unsigned long)atomic_load(&state.reads_completed));
        printf("}\n");
    } else {
        // Human-readable output
        printf("\n");
        printf("Test Results:\n");
        printf("=============\n\n");
        printf("Operations:       %lu\n", (unsigned long)atomic_load(&state.operations));
        printf("Bugs found:       %lu\n", (unsigned long)atomic_load(&state.bugs_found));
        printf("Reads completed:  %lu\n", (unsigned long)atomic_load(&state.reads_completed));
        
        if (atomic_load(&state.operations) > 0) {
            double bug_rate = (double)atomic_load(&state.bugs_found) * 100000.0 / 
                             (double)atomic_load(&state.operations);
            printf("Bug rate:         %.1f per 100K operations\n", bug_rate);
        }
        
        // Workload stats
        char workload_stats[512];
        workload->get_stats(&state, workload_stats, sizeof(workload_stats));
        printf("\nWorkload Stats:\n");
        printf("  %s\n", workload_stats);
        
        // Injector stats
        if (injector_ctrl) {
            printf("\n");
            controller_print_stats(injector_ctrl, stdout);
        }
    }
    
    // Cleanup
    free(reader_threads);
    free(writer_threads);
    workload->cleanup(&state);
    free(bug_queue);
    tracker_cleanup(tracker);
    
    if (injector_ctrl) {
        controller_unload(injector_ctrl);
    }
    
    g_state = NULL;
    
    return 0;
}

