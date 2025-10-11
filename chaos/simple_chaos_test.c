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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "../common/dir_reader.h"
#include "../common/state_tracker.h"

/**
 * Xibalba Chaos Test - The Dark House Trial
 * 
 * Tests: Concurrent directory reading with validation
 * 
 * This test creates a directory, performs concurrent creates/deletes/reads,
 * and validates that all reads are correct using ground truth state tracking.
 * 
 * Supports multiple consistency models:
 *   --strict:    Linearizable (strictest, finds most bugs)
 *   --weak:      POSIX weak consistency (default)
 *   --eventual:  Eventual consistency (most permissive)
 * 
 * With eBPF delays, this should find race conditions.
 * Without delays, stable kernels should show zero bugs (or few with strict model).
 */

// Default configuration (can be overridden by command line args)
#define DEFAULT_NUM_READER_THREADS 10
#define DEFAULT_NUM_WRITER_THREADS 3
#define DEFAULT_TEST_DURATION 300  // seconds (5 minutes - worth the VM overhead)
#define FILES_PER_WRITER 50  // Files created per writer thread (not configurable)

struct test_state {
    const char *test_dir;
    state_tracker_t *tracker;
    consistency_model_t model;
    atomic_bool stop;
    _Atomic uint64_t operations;
    _Atomic uint64_t bugs_found;
    _Atomic uint64_t reads_completed;
};

/* Reader thread: Continuously scans directory and validates results */
static void *reader_thread(void *arg) {
    struct test_state *state = (struct test_state *)arg;
    struct dir_reader *reader = dir_reader_create_classic();
    uint64_t thread_id = (uint64_t)pthread_self();
    
    while (!atomic_load(&state->stop)) {
        // Open directory
        if (dir_reader_open(reader, state->test_dir) < 0) {
            usleep(1000);
            continue;
        }
        
        // Record read start time (snapshot point for POSIX weak consistency)
        struct timespec ts_start, ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        uint64_t read_start_ns = (uint64_t)ts_start.tv_sec * 1000000000ULL + (uint64_t)ts_start.tv_nsec;
        tracker_record_read_start(state->tracker, thread_id);
        
        // Read all entries
        struct dir_entry entries[200];
        char *entry_names[200];
        int total_read = 0;
        int count;
        
        while ((count = dir_reader_read(reader, entries, 100)) > 0) {
            for (int i = 0; i < count && total_read < 200; i++) {
                // Skip . and ..
                if (strcmp(entries[i].name, ".") == 0 || 
                    strcmp(entries[i].name, "..") == 0) {
                    continue;
                }
                
                // Track each entry read
                tracker_record_read_entry(state->tracker, entries[i].name, thread_id);
                entry_names[total_read] = strdup(entries[i].name);
                total_read++;
            }
        }
        
        // Record read end time
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        uint64_t read_end_ns = (uint64_t)ts_end.tv_sec * 1000000000ULL + (uint64_t)ts_end.tv_nsec;
        tracker_record_read_end(state->tracker, thread_id);
        
        // VALIDATE: Check if what we read matches expected state
        validation_result_t result = tracker_validate_read(
            state->tracker, 
            entry_names, 
            total_read,
            read_start_ns,
            read_end_ns,
            state->model  // Use configured consistency model
        );
        
        // Report bugs found
        if (result.total_bugs_found > 0) {
            atomic_fetch_add(&state->bugs_found, result.total_bugs_found);
            
            printf("🐛 BUG FOUND (thread %lu):\n", thread_id);
            if (result.missing_entries > 0) {
                printf("   Missing entries: %lu\n", result.missing_entries);
            }
            if (result.duplicate_entries > 0) {
                printf("   Duplicate entries: %lu\n", result.duplicate_entries);
            }
            if (result.phantom_entries > 0) {
                printf("   Phantom entries: %lu\n", result.phantom_entries);
            }
        }
        
        // Cleanup
        for (int i = 0; i < total_read; i++) {
            free(entry_names[i]);
        }
        
        dir_reader_close(reader);
        atomic_fetch_add(&state->operations, 1);
        atomic_fetch_add(&state->reads_completed, 1);
        
        usleep(100);  // Brief pause
    }
    
    dir_reader_destroy(reader);
    return NULL;
}

/* Writer thread: Creates and deletes files to cause mutations */
static void *writer_thread(void *arg) {
    struct test_state *state = (struct test_state *)arg;
    uint64_t thread_id = (uint64_t)pthread_self();
    char filepath[512];
    
    int file_num = 0;
    
    while (!atomic_load(&state->stop)) {
        // Create a file
        snprintf(filepath, sizeof(filepath), "%s/writer_%lu_file_%d", 
                 state->test_dir, thread_id, file_num);
        
        int fd = open(filepath, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) {
            close(fd);
            
            // Track creation
            char filename[256];
            snprintf(filename, sizeof(filename), "writer_%lu_file_%d", 
                     thread_id, file_num);
            tracker_record_create(state->tracker, filename);
            
            atomic_fetch_add(&state->operations, 1);
        }
        
        // Sometimes delete previous file
        if (file_num > 0 && (file_num % 3) == 0) {
            snprintf(filepath, sizeof(filepath), "%s/writer_%lu_file_%d",
                     state->test_dir, thread_id, file_num - 1);
            
            if (unlink(filepath) == 0) {
                // Track deletion
                char filename[256];
                snprintf(filename, sizeof(filename), "writer_%lu_file_%d",
                         thread_id, file_num - 1);
                tracker_record_delete(state->tracker, filename);
                
                atomic_fetch_add(&state->operations, 1);
            }
        }
        
        file_num++;
        if (file_num >= FILES_PER_WRITER) {
            file_num = 0;  // Wrap around
        }
        
        usleep(10000);  // 10ms between operations
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [OPTIONS] <directory>\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Xibalba Chaos Test - The Dark House Trial\n");
        fprintf(stderr, "Tests concurrent directory operations with validation.\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  --strict    Strict/linearizable consistency (most bugs detected)\n");
        fprintf(stderr, "  --weak      POSIX weak consistency (default)\n");
        fprintf(stderr, "  --eventual  Eventual consistency (only duplicates are bugs)\n");
        fprintf(stderr, "  --json      Output results as JSON (for benchmarking)\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "  mkdir -p /tmp/xibalba_test\n");
        fprintf(stderr, "  %s /tmp/xibalba_test\n", argv[0]);
        fprintf(stderr, "  %s --strict /tmp/xibalba_test  # Strictest validation\n", argv[0]);
        fprintf(stderr, "  %s --json --weak /tmp/test > results.json\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "With eBPF delays (separate terminal):\n");
        fprintf(stderr, "  bazel run //chaos:pause_controller -- 50 500\n");
        return 1;
    }
    
    // Parse arguments
    consistency_model_t model = CONSISTENCY_WEAK_POSIX;  // Default
    const char *test_dir = NULL;
    bool json_output = false;
    int test_duration = DEFAULT_TEST_DURATION;
    int num_readers = DEFAULT_NUM_READER_THREADS;
    int num_writers = DEFAULT_NUM_WRITER_THREADS;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strict") == 0) {
            model = CONSISTENCY_STRICT;
        } else if (strcmp(argv[i], "--weak") == 0) {
            model = CONSISTENCY_WEAK_POSIX;
        } else if (strcmp(argv[i], "--eventual") == 0) {
            model = CONSISTENCY_EVENTUAL;
        } else if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            test_duration = atoi(argv[++i]);
            if (test_duration < 1 || test_duration > 3600) {
                fprintf(stderr, "Error: Duration must be 1-3600 seconds\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--readers") == 0 && i + 1 < argc) {
            num_readers = atoi(argv[++i]);
            if (num_readers < 1 || num_readers > 100) {
                fprintf(stderr, "Error: Readers must be 1-100\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--writers") == 0 && i + 1 < argc) {
            num_writers = atoi(argv[++i]);
            if (num_writers < 1 || num_writers > 100) {
                fprintf(stderr, "Error: Writers must be 1-100\n");
                return 1;
            }
        } else {
            test_dir = argv[i];
        }
    }
    
    if (!test_dir) {
        fprintf(stderr, "Error: No directory specified\n");
        return 1;
    }
    
    // Verify directory exists
    struct stat st;
    if (stat(test_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: %s is not a directory\n", test_dir);
        return 1;
    }
    
    const char *model_name = 
        model == CONSISTENCY_STRICT ? "Strict/Linearizable" :
        model == CONSISTENCY_WEAK_POSIX ? "POSIX Weak" :
        "Eventual";
    
    const char *model_short =
        model == CONSISTENCY_STRICT ? "strict" :
        model == CONSISTENCY_WEAK_POSIX ? "weak" :
        "eventual";
    
    if (!json_output) {
        printf("=== Xibalba Chaos Test ===\n");
        printf("The Dark House Trial: Race Conditions in Darkness\n");
        printf("\n");
        printf("Directory: %s\n", test_dir);
        printf("Consistency model: %s\n", model_name);
        printf("Reader threads: %d\n", num_readers);
        printf("Writer threads: %d\n", num_writers);
        printf("Duration: %d seconds\n", test_duration);
        printf("\n");
    }
    
    // Initialize state tracker (ground truth)
    state_tracker_t *tracker = tracker_init();
    if (!tracker) {
        fprintf(stderr, "Failed to initialize state tracker\n");
        return 1;
    }
    
    struct test_state state = {
        .test_dir = test_dir,
        .tracker = tracker,
        .model = model,
        .stop = ATOMIC_VAR_INIT(false),
        .operations = ATOMIC_VAR_INIT(0),
        .bugs_found = ATOMIC_VAR_INIT(0),
        .reads_completed = ATOMIC_VAR_INIT(0),
    };
    
    if (!json_output) {
        printf("Starting test...\n");
        printf("\n");
    }
    
    // Allocate thread arrays based on configuration
    pthread_t *reader_threads = malloc(sizeof(pthread_t) * (size_t)num_readers);
    pthread_t *writer_threads = malloc(sizeof(pthread_t) * (size_t)num_writers);
    
    if (!reader_threads || !writer_threads) {
        fprintf(stderr, "Failed to allocate thread arrays\n");
        return 1;
    }
    
    // Launch reader threads
    for (int i = 0; i < num_readers; i++) {
        if (pthread_create(&reader_threads[i], NULL, reader_thread, &state) != 0) {
            fprintf(stderr, "Failed to create reader thread %d\n", i);
            return 1;
        }
    }
    
    // Launch writer threads
    for (int i = 0; i < num_writers; i++) {
        if (pthread_create(&writer_threads[i], NULL, writer_thread, &state) != 0) {
            fprintf(stderr, "Failed to create writer thread %d\n", i);
            return 1;
        }
    }
    
    // Run for duration
    sleep((unsigned int)test_duration);
    
    // Stop all threads
    if (!json_output) {
        printf("Stopping threads...\n");
    }
    atomic_store(&state.stop, true);
    
    for (int i = 0; i < num_readers; i++) {
        pthread_join(reader_threads[i], NULL);
    }
    for (int i = 0; i < num_writers; i++) {
        pthread_join(writer_threads[i], NULL);
    }
    
    // Free thread arrays
    free(reader_threads);
    free(writer_threads);
    
    // Print results
    uint64_t ops = atomic_load(&state.operations);
    uint64_t bugs = atomic_load(&state.bugs_found);
    uint64_t reads = atomic_load(&state.reads_completed);
    double bug_rate = reads > 0 ? (double)bugs / (double)reads : 0.0;
    double ops_per_sec = (double)ops / (double)test_duration;
    
    if (json_output) {
        // Machine-readable JSON output for benchmarking
        printf("{\n");
        printf("  \"test\": \"xibalba_chaos\",\n");
        printf("  \"directory\": \"%s\",\n", test_dir);
        printf("  \"consistency_model\": \"%s\",\n", model_short);
        printf("  \"duration_seconds\": %d,\n", test_duration);
        printf("  \"reader_threads\": %d,\n", num_readers);
        printf("  \"writer_threads\": %d,\n", num_writers);
        printf("  \"results\": {\n");
        printf("    \"total_operations\": %lu,\n", ops);
        printf("    \"directory_scans\": %lu,\n", reads);
        printf("    \"ops_per_second\": %.2f,\n", ops_per_sec);
        printf("    \"bugs_found\": %lu,\n", bugs);
        printf("    \"bug_rate\": %.6f,\n", bug_rate);
        printf("    \"bugs_per_1000_scans\": %.2f\n", bug_rate * 1000.0);
        printf("  }\n");
        printf("}\n");
        
        tracker_cleanup(tracker);
        return bugs > 0 ? 1 : 0;
    }
    
    // Human-readable output
    printf("\n");
    printf("=== Results ===\n");
    printf("\n");
    printf("Operations:\n");
    printf("  Total operations: %lu\n", ops);
    printf("  Directory scans:  %lu\n", reads);
    printf("  Ops/second:       %.1f\n", ops_per_sec);
    printf("\n");
    printf("Validation:\n");
    printf("  Bugs found: %lu\n", bugs);
    printf("  Bug rate: %.6f (%.2f per 1000 scans)\n", bug_rate, bug_rate * 1000.0);
    
    if (bugs == 0) {
        printf("  Status: ✅ NO BUGS DETECTED\n");
        printf("\n");
        printf("This means:\n");
        printf("  - Validation logic works correctly\n");
        printf("  - Host kernel is stable (or race windows too narrow)\n");
        printf("  - Try with eBPF delays to widen race windows:\n");
        printf("    bazel run //chaos:pause_controller -- 50 500\n");
    } else {
        printf("  Status: 🐛 BUGS DETECTED!\n");
        printf("\n");
        printf("Race conditions found in directory operations!\n");
        printf("This proves:\n");
        printf("  - Validation works (bugs were detected)\n");
        printf("  - eBPF delays widen race windows (if running)\n");
        printf("  - Code has concurrency issues that need fixing\n");
    }
    
    printf("\n");
    
    // Export history for analysis
    char history_file[512];
    snprintf(history_file, sizeof(history_file), "%s/xibalba-history.json", test_dir);
    tracker_export_history(tracker, history_file);
    printf("Operation history exported to: %s\n", history_file);
    
    // Cleanup
    tracker_cleanup(tracker);
    
    printf("\n");
    if (bugs > 0) {
        printf("⚠️  FAIL: Bugs found (exit 1 for CI)\n");
        return 1;  // Fail CI if bugs found
    } else {
        printf("✅ PASS: No bugs detected\n");
        return 0;
    }
}
