/*
 * Vector Clock Implementation for Causality Tracking
 * 
 * This is the foundation for Jepsen-style distributed systems testing.
 * 
 * What are vector clocks?
 *   - Each thread/process has a logical clock (not wall time!)
 *   - Clock increments on each operation
 *   - Clocks are merged when operations communicate
 *   - Enables tracking happens-before relationships
 * 
 * Why vector clocks instead of timestamps?
 *   - Timestamps can't detect causality (clock skew, races)
 *   - Vector clocks capture true happens-before relationships
 *   - Example:
 *       Thread A creates file → VC[A=1, B=0]
 *       Thread B reads directory → VC[A=?, B=1]
 *       If B's clock shows A=1, then B MUST see the file!
 *       If B's clock shows A=0, then race condition (either valid)
 * 
 * Jepsen uses this exact approach for detecting linearizability violations!
 */

#ifndef VECTOR_CLOCK_H
#define VECTOR_CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define MAX_THREADS 64  // Maximum concurrent threads to track

// Vector clock: one counter per thread
typedef struct {
    uint64_t clocks[MAX_THREADS];
    uint32_t num_threads;
    pthread_mutex_t lock;
} vector_clock_t;

// Thread ID to clock index mapping
typedef struct {
    pthread_t thread_id;
    uint32_t clock_idx;
} thread_mapping_t;

// Initialize a vector clock
vector_clock_t* vclock_init(void);

// Get clock index for current thread (creates if needed)
uint32_t vclock_get_thread_idx(vector_clock_t *vc, pthread_t thread_id);

// Increment clock for current thread (happens on every operation)
void vclock_tick(vector_clock_t *vc, pthread_t thread_id);

// Take a snapshot of current clock state
void vclock_snapshot(vector_clock_t *vc, uint64_t *snapshot);

// Check if event A happens-before event B
// Returns true if A definitely happened before B (causality established)
// Returns false if concurrent or B-before-A (no causality)
bool vclock_happens_before(const uint64_t *clock_a, const uint64_t *clock_b, uint32_t num_threads);

// Merge two vector clocks (for coordination points)
void vclock_merge(vector_clock_t *vc, const uint64_t *other_clock);

// Cleanup
void vclock_cleanup(vector_clock_t *vc);

// Print vector clock (for debugging)
void vclock_print(const uint64_t *clock, uint32_t num_threads);

#endif // VECTOR_CLOCK_H

