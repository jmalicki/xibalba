#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "../common/dir_reader.h"

/**
 * Simple Chaos Test - Tech De-Risking PoC
 * 
 * Goal: Test concurrent directory reading with basic chaos
 * No fault injection yet - just prove concurrent reading works
 */

#define NUM_THREADS 10
#define TEST_DURATION 5  // 5 seconds for quick test

struct test_state {
    const char *test_dir;
    atomic_bool stop;
    atomic_int operations;
    atomic_int total_entries;
};

static void *reader_thread(void *arg) {
    struct test_state *state = arg;
    struct dir_reader *reader = dir_reader_create_classic();
    
    while (!atomic_load(&state->stop)) {
        if (dir_reader_open(reader, state->test_dir) < 0) {
            usleep(1000);
            continue;
        }
        
        struct dir_entry entries[100];
        int count;
        int entries_this_scan = 0;
        
        while ((count = dir_reader_read(reader, entries, 100)) > 0) {
            entries_this_scan += count;
        }
        
        dir_reader_close(reader);
        atomic_fetch_add(&state->operations, 1);
        atomic_fetch_add(&state->total_entries, entries_this_scan);
        
        // Brief pause between scans
        usleep(100);
    }
    
    dir_reader_destroy(reader);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "  mkdir -p /tmp/rudra_test\n");
        fprintf(stderr, "  touch /tmp/rudra_test/file{1..100}\n");
        fprintf(stderr, "  %s /tmp/rudra_test\n", argv[0]);
        return 1;
    }
    
    struct test_state state = {
        .test_dir = argv[1],
        .stop = false,
        .operations = 0,
        .total_entries = 0,
    };
    
    printf("=== RUDRA Simple Chaos Test ===\n");
    printf("Directory: %s\n", state.test_dir);
    printf("Threads: %d\n", NUM_THREADS);
    printf("Duration: %d seconds\n", TEST_DURATION);
    printf("\n");
    printf("Starting test...\n");
    
    pthread_t threads[NUM_THREADS];
    
    // Launch threads
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, reader_thread, &state) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            return 1;
        }
    }
    
    // Run for duration
    sleep(TEST_DURATION);
    
    // Stop all threads
    atomic_store(&state.stop, true);
    
    printf("Stopping threads...\n");
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Print results
    int ops = atomic_load(&state.operations);
    int entries = atomic_load(&state.total_entries);
    
    printf("\n=== Results ===\n");
    printf("Operations completed: %d\n", ops);
    printf("Total entries read: %d\n", entries);
    printf("Operations/second: %.1f\n", (double)ops / (double)TEST_DURATION);
    printf("\n");
    printf("✅ PASS: No crashes detected\n");
    printf("\n");
    printf("Next step: Add eBPF pause injection to increase race detection\n");
    
    return 0;
}

