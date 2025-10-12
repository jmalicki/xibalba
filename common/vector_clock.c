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
#include <stdint.h>  // For uintptr_t

vector_clock_t* vclock_init(uint32_t max_threads) {
    // Default to 1024 if not specified
    if (max_threads == 0) {
        max_threads = 1024;
    }
    
    vector_clock_t *vc = calloc(1, sizeof(vector_clock_t));
    if (!vc) return NULL;
    
    // Allocate dynamic arrays
    vc->max_threads = max_threads;
    vc->clocks = calloc(max_threads, sizeof(uint64_t));
    vc->thread_ids = calloc(max_threads, sizeof(pthread_t));
    
    if (!vc->clocks || !vc->thread_ids) {
        free(vc->clocks);
        free(vc->thread_ids);
        free(vc);
        return NULL;
    }
    
    pthread_mutex_init(&vc->lock, NULL);
    pthread_mutex_init(&vc->registry_lock, NULL);
    
    // Create TLS key for fast thread index lookup
    if (pthread_key_create(&vc->tls_key, NULL) != 0) {
        free(vc->clocks);
        free(vc->thread_ids);
        free(vc);
        return NULL;
    }
    
    vc->num_registered = 0;
    
    return vc;
}

uint32_t vclock_get_thread_idx(vector_clock_t *vc, pthread_t thread_id) {
    // Use pthread_self() as the actual thread ID, ignoring the parameter
    // (parameter exists for API compatibility but TLS is keyed by calling thread)
    pthread_t actual_thread = pthread_self();
    (void)thread_id;  // Unused - kept for API compatibility
    
    // Fast path: Check thread-local storage
    // After first registration, this is O(1) with no mutex!
    void *idx_ptr = pthread_getspecific(vc->tls_key);
    if (idx_ptr != NULL) {
        return (uint32_t)(uintptr_t)idx_ptr;
    }
    
    // Slow path: First time for this thread - register it
    pthread_mutex_lock(&vc->registry_lock);
    
    // Double-check: another thread might have registered while we waited
    idx_ptr = pthread_getspecific(vc->tls_key);
    if (idx_ptr != NULL) {
        pthread_mutex_unlock(&vc->registry_lock);
        return (uint32_t)(uintptr_t)idx_ptr;
    }
    
    // Check if we have space for another thread
    if (vc->num_registered >= vc->max_threads) {
        pthread_mutex_unlock(&vc->registry_lock);
        fprintf(stderr, "FATAL: Exceeded max_threads (%u)\n", vc->max_threads);
        abort();
    }
    
    // Register this thread
    uint32_t idx = vc->num_registered++;
    vc->thread_ids[idx] = actual_thread;  // Store actual thread ID
    
    // Store in TLS for future O(1) lookups
    pthread_setspecific(vc->tls_key, (void*)(uintptr_t)idx);
    
    pthread_mutex_unlock(&vc->registry_lock);
    return idx;
}

void vclock_tick(vector_clock_t *vc, pthread_t thread_id) {
    // Get thread's clock index (fast TLS lookup after first call)
    uint32_t idx = vclock_get_thread_idx(vc, thread_id);
    
    // Increment this thread's clock
    pthread_mutex_lock(&vc->lock);
    vc->clocks[idx]++;
    pthread_mutex_unlock(&vc->lock);
}

void vclock_snapshot(vector_clock_t *vc, uint64_t *snapshot) {
    pthread_mutex_lock(&vc->lock);
    memcpy(snapshot, vc->clocks, sizeof(uint64_t) * vc->max_threads);
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
    for (uint32_t i = 0; i < vc->max_threads; i++) {
        if (other_clock[i] > vc->clocks[i]) {
            vc->clocks[i] = other_clock[i];
        }
    }
    
    pthread_mutex_unlock(&vc->lock);
}

void vclock_cleanup(vector_clock_t *vc) {
    if (vc) {
        pthread_key_delete(vc->tls_key);
        pthread_mutex_destroy(&vc->lock);
        pthread_mutex_destroy(&vc->registry_lock);
        free(vc->clocks);
        free(vc->thread_ids);
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

