/*
 * Vector Clock Implementation
 * 
 * Based on Lamport's "Time, Clocks, and the Ordering of Events" (1978)
 * and Fidge/Mattern vector clocks (1988).
 */

#include "vector_clock.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

vector_clock_t* vclock_init(void) {
    vector_clock_t *vc = calloc(1, sizeof(vector_clock_t));
    if (!vc) return NULL;
    
    pthread_mutex_init(&vc->lock, NULL);
    vc->num_threads = 0;
    memset(vc->clocks, 0, sizeof(vc->clocks));
    
    return vc;
}

uint32_t vclock_get_thread_idx(vector_clock_t *vc, pthread_t thread_id) {
    // Simple hash-based mapping: use thread_id modulo MAX_THREADS
    // This works for controlled test scenarios with known thread counts
    // For production with many threads, would need proper hash table
    
    (void)vc;  // Not used in simple approach
    uint32_t idx = ((uint64_t)thread_id / 1000) % MAX_THREADS;
    return idx;
}

void vclock_tick(vector_clock_t *vc, pthread_t thread_id) {
    pthread_mutex_lock(&vc->lock);
    
    // Simple approach: use thread_id modulo MAX_THREADS as index
    // This works for our use case (controlled # of threads)
    uint32_t idx = ((uint64_t)thread_id / 1000) % MAX_THREADS;
    
    if (idx < MAX_THREADS) {
        vc->clocks[idx]++;
        if (idx >= vc->num_threads) {
            vc->num_threads = idx + 1;
        }
    }
    
    pthread_mutex_unlock(&vc->lock);
}

void vclock_snapshot(vector_clock_t *vc, uint64_t *snapshot) {
    pthread_mutex_lock(&vc->lock);
    memcpy(snapshot, vc->clocks, sizeof(uint64_t) * MAX_THREADS);
    pthread_mutex_unlock(&vc->lock);
}

bool vclock_happens_before(const uint64_t *clock_a, const uint64_t *clock_b, uint32_t num_threads) {
    // A happens-before B if:
    //   - For all threads i: A[i] <= B[i]
    //   - For at least one thread j: A[j] < B[j]
    
    bool all_less_or_equal = true;
    bool at_least_one_less = false;
    
    for (uint32_t i = 0; i < num_threads; i++) {
        if (clock_a[i] > clock_b[i]) {
            all_less_or_equal = false;
            break;
        }
        if (clock_a[i] < clock_b[i]) {
            at_least_one_less = true;
        }
    }
    
    return all_less_or_equal && at_least_one_less;
}

void vclock_merge(vector_clock_t *vc, const uint64_t *other_clock) {
    pthread_mutex_lock(&vc->lock);
    
    // Merge: take max of each component
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        if (other_clock[i] > vc->clocks[i]) {
            vc->clocks[i] = other_clock[i];
        }
    }
    
    pthread_mutex_unlock(&vc->lock);
}

void vclock_cleanup(vector_clock_t *vc) {
    if (vc) {
        pthread_mutex_destroy(&vc->lock);
        free(vc);
    }
}

void vclock_print(const uint64_t *clock, uint32_t num_threads) {
    printf("[");
    for (uint32_t i = 0; i < num_threads; i++) {
        printf("%lu", clock[i]);
        if (i < num_threads - 1) printf(", ");
    }
    printf("]");
}

