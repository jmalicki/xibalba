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

#define NUM_READER_THREADS 10
#define NUM_WRITER_THREADS 3
#define TEST_DURATION 5  // seconds
#define FILES_PER_WRITER 50

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
        fprintf(stderr, "\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "  mkdir -p /tmp/xibalba_test\n");
        fprintf(stderr, "  %s /tmp/xibalba_test\n", argv[0]);
        fprintf(stderr, "  %s --strict /tmp/xibalba_test  # Strictest validation\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "With eBPF delays (separate terminal):\n");
        fprintf(stderr, "  bazel run //chaos:pause_controller -- 50 500\n");
        return 1;
    }
    
    // Parse arguments
    consistency_model_t model = CONSISTENCY_WEAK_POSIX;  // Default
    const char *test_dir = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strict") == 0) {
            model = CONSISTENCY_STRICT;
        } else if (strcmp(argv[i], "--weak") == 0) {
            model = CONSISTENCY_WEAK_POSIX;
        } else if (strcmp(argv[i], "--eventual") == 0) {
            model = CONSISTENCY_EVENTUAL;
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
    
    printf("=== Xibalba Chaos Test ===\n");
    printf("The Dark House Trial: Race Conditions in Darkness\n");
    printf("\n");
    printf("Directory: %s\n", test_dir);
    printf("Consistency model: %s\n", model_name);
    printf("Reader threads: %d\n", NUM_READER_THREADS);
    printf("Writer threads: %d\n", NUM_WRITER_THREADS);
    printf("Duration: %d seconds\n", TEST_DURATION);
    printf("\n");
    
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
    
    printf("Starting test...\n");
    printf("\n");
    
    pthread_t reader_threads[NUM_READER_THREADS];
    pthread_t writer_threads[NUM_WRITER_THREADS];
    
    // Launch reader threads
    for (int i = 0; i < NUM_READER_THREADS; i++) {
        if (pthread_create(&reader_threads[i], NULL, reader_thread, &state) != 0) {
            fprintf(stderr, "Failed to create reader thread %d\n", i);
            return 1;
        }
    }
    
    // Launch writer threads
    for (int i = 0; i < NUM_WRITER_THREADS; i++) {
        if (pthread_create(&writer_threads[i], NULL, writer_thread, &state) != 0) {
            fprintf(stderr, "Failed to create writer thread %d\n", i);
            return 1;
        }
    }
    
    // Run for duration
    sleep(TEST_DURATION);
    
    // Stop all threads
    printf("Stopping threads...\n");
    atomic_store(&state.stop, true);
    
    for (int i = 0; i < NUM_READER_THREADS; i++) {
        pthread_join(reader_threads[i], NULL);
    }
    for (int i = 0; i < NUM_WRITER_THREADS; i++) {
        pthread_join(writer_threads[i], NULL);
    }
    
    // Print results
    uint64_t ops = atomic_load(&state.operations);
    uint64_t bugs = atomic_load(&state.bugs_found);
    uint64_t reads = atomic_load(&state.reads_completed);
    
    printf("\n");
    printf("=== Results ===\n");
    printf("\n");
    printf("Operations:\n");
    printf("  Total operations: %lu\n", ops);
    printf("  Directory scans:  %lu\n", reads);
    printf("  Ops/second:       %.1f\n", (double)ops / (double)TEST_DURATION);
    printf("\n");
    printf("Validation:\n");
    printf("  Bugs found: %lu\n", bugs);
    
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
