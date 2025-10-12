/*
 * Comprehensive Vector Clock Tests
 * 
 * These tests validate the core causality tracking mechanism.
 * Vector clocks are THE foundation for bug detection - if these
 * are wrong, everything else fails!
 */

#include "vector_clock.h"
#include <gtest/gtest.h>
#include <pthread.h>
#include <vector>
#include <set>

// For test convenience - matches default vclock_init(0)
#define MAX_THREADS 1024

class VectorClockTest : public ::testing::Test {
protected:
    vector_clock_t *vc;
    
    void SetUp() override {
        vc = vclock_init(0);  // 0 = use default max_threads
        ASSERT_NE(vc, nullptr);
    }
    
    void TearDown() override {
        vclock_cleanup(vc);
    }
    
    // Helper: create a fake thread ID
    pthread_t make_thread_id(uint64_t id) {
        return (pthread_t)id;
    }
};

// ============================================================================
// BASIC OPERATIONS
// ============================================================================

TEST_F(VectorClockTest, InitializesAllClocksToZero) {
    // Test: New vector clock should have all zeros
    // Requirement: Causality tracking requires known initial state
    
    uint64_t *snapshot = new uint64_t[vc->max_threads];
    vclock_snapshot(vc, snapshot);
    
    for (uint32_t i = 0; i < vc->max_threads; i++) {
        EXPECT_EQ(snapshot[i], 0) << "Clock " << i << " not initialized to 0";
    }
    delete[] snapshot;
}

struct TickData {
    vector_clock_t *vc;
    int num_ticks;
    uint32_t *out_idx;
};

static void* tick_n_times(void *arg) {
    TickData *data = (TickData*)arg;
    pthread_t self = pthread_self();
    for (int i = 0; i < data->num_ticks; i++) {
        vclock_tick(data->vc, self);
    }
    if (data->out_idx) {
        *data->out_idx = vclock_get_thread_idx(data->vc, self);
    }
    return nullptr;
}

TEST_F(VectorClockTest, TickIncrementsCorrectSlot) {
    // Test: vclock_tick should increment only the thread's slot
    // Requirement: Each thread must have independent logical clock
    
    uint32_t idx1, idx2;
    
    // Thread 1: tick twice
    pthread_t t1;
    TickData data1 = {vc, 2, &idx1};
    pthread_create(&t1, nullptr, tick_n_times, &data1);
    pthread_join(t1, nullptr);
    
    // Thread 2: tick once
    pthread_t t2;
    TickData data2 = {vc, 1, &idx2};
    pthread_create(&t2, nullptr, tick_n_times, &data2);
    pthread_join(t2, nullptr);
    
    uint64_t *snapshot = new uint64_t[vc->max_threads];
    vclock_snapshot(vc, snapshot);
    
    EXPECT_EQ(snapshot[idx1], 2) << "Thread 1 clock not incremented correctly";
    EXPECT_EQ(snapshot[idx2], 1) << "Thread 2 clock not incremented correctly";
    EXPECT_NE(idx1, idx2) << "Threads should have different indices";
}

TEST_F(VectorClockTest, SnapshotCapturesCurrentState) {
    // Test: Snapshot should be immutable copy of current state
    // Requirement: We need stable snapshots for happens-before checks
    
    pthread_t thread_1 = make_thread_id(1000);
    
    vclock_tick(vc, thread_1);
    
    uint64_t snapshot_before[MAX_THREADS];
    vclock_snapshot(vc, snapshot_before);
    
    vclock_tick(vc, thread_1);  // Modify after snapshot
    
    uint64_t snapshot_after[MAX_THREADS];
    vclock_snapshot(vc, snapshot_after);
    
    uint32_t idx = vclock_get_thread_idx(vc, thread_1);
    EXPECT_EQ(snapshot_before[idx], 1);
    EXPECT_EQ(snapshot_after[idx], 2);
}

// ============================================================================
// HAPPENS-BEFORE LOGIC (CRITICAL!)
// ============================================================================

TEST_F(VectorClockTest, HappensBeforeBasicSequence) {
    // Test: Operation A before B should be detected
    // Requirement: If A→B causally, we must detect it!
    
    pthread_t thread_1 = make_thread_id(1000);
    
    vclock_tick(vc, thread_1);  // Event A
    uint64_t clock_a[MAX_THREADS];
    vclock_snapshot(vc, clock_a);
    
    vclock_tick(vc, thread_1);  // Event B (after A)
    uint64_t clock_b[MAX_THREADS];
    vclock_snapshot(vc, clock_b);
    
    EXPECT_TRUE(vclock_happens_before(clock_a, clock_b, MAX_THREADS))
        << "A should happen-before B (same thread, sequential)";
    EXPECT_FALSE(vclock_happens_before(clock_b, clock_a, MAX_THREADS))
        << "B cannot happen-before A (time doesn't go backwards)";
}

TEST_F(VectorClockTest, HappensBeforeConcurrentEvents) {
    // Test: Concurrent events should NOT have happens-before
    // Requirement: Avoid false causality between independent operations
    
    pthread_t thread_1 = make_thread_id(1000);
    pthread_t thread_2 = make_thread_id(2000);
    
    vclock_tick(vc, thread_1);
    uint64_t clock_a[MAX_THREADS];
    vclock_snapshot(vc, clock_a);
    
    vclock_tick(vc, thread_2);
    uint64_t clock_b[MAX_THREADS];
    vclock_snapshot(vc, clock_b);
    
    EXPECT_FALSE(vclock_happens_before(clock_a, clock_b, MAX_THREADS))
        << "Concurrent events should not have happens-before (A→B)";
    EXPECT_FALSE(vclock_happens_before(clock_b, clock_a, MAX_THREADS))
        << "Concurrent events should not have happens-before (B→A)";
}

TEST_F(VectorClockTest, HappensBeforeIdenticalClocks) {
    // Test: Identical clocks = same event, not happens-before
    // Requirement: Happens-before requires STRICT ordering
    
    pthread_t thread_1 = make_thread_id(1000);
    
    vclock_tick(vc, thread_1);
    uint64_t clock_a[MAX_THREADS];
    vclock_snapshot(vc, clock_a);
    
    uint64_t clock_b[MAX_THREADS];
    vclock_snapshot(vc, clock_b);  // Same snapshot
    
    EXPECT_FALSE(vclock_happens_before(clock_a, clock_b, MAX_THREADS))
        << "Identical clocks should not have happens-before";
}

TEST_F(VectorClockTest, HappensBeforeWithMerge) {
    // Test: Merge establishes causality across threads
    // Requirement: When threads coordinate, causality must propagate
    
    pthread_t thread_1 = make_thread_id(1000);
    pthread_t thread_2 = make_thread_id(2000);
    
    // Thread 1 does work
    vclock_tick(vc, thread_1);
    vclock_tick(vc, thread_1);
    uint64_t clock_t1_work[MAX_THREADS];
    vclock_snapshot(vc, clock_t1_work);
    
    // Thread 2 merges (sees Thread 1's work)
    vclock_merge(vc, clock_t1_work);
    vclock_tick(vc, thread_2);
    uint64_t clock_t2_after_merge[MAX_THREADS];
    vclock_snapshot(vc, clock_t2_after_merge);
    
    // Thread 1's work happens-before Thread 2's observation
    EXPECT_TRUE(vclock_happens_before(clock_t1_work, clock_t2_after_merge, MAX_THREADS))
        << "Thread 1's work should happen-before Thread 2's post-merge tick";
}

// ============================================================================
// THREAD ID HASHING (POTENTIAL BUG SOURCE!)
// ============================================================================

TEST_F(VectorClockTest, ThreadIdMappingIsConsistent) {
    // Test: Same thread ID always maps to same slot
    // Requirement: Thread must have stable clock slot
    // Note: With TLS, this uses pthread_self() so we test from main thread
    
    pthread_t self = pthread_self();
    
    uint32_t idx1 = vclock_get_thread_idx(vc, self);
    uint32_t idx2 = vclock_get_thread_idx(vc, self);
    uint32_t idx3 = vclock_get_thread_idx(vc, self);
    
    EXPECT_EQ(idx1, idx2);
    EXPECT_EQ(idx2, idx3);
    EXPECT_EQ(idx1, 0u) << "First thread should get index 0";
}

TEST_F(VectorClockTest, ZeroCollisionsWithTLS) {
    // Test: TLS-based registry ensures zero collisions with real threads
    // Requirement: Each thread must have unique clock slot
    
    const int NUM_TEST_THREADS = 10;
    std::vector<uint32_t> indices(NUM_TEST_THREADS);
    std::vector<pthread_t> threads(NUM_TEST_THREADS);
    
    // Spawn real threads to test TLS
    for (int i = 0; i < NUM_TEST_THREADS; i++) {
        TickData data = {vc, 1, &indices[(size_t)i]};
        pthread_create(&threads[(size_t)i], nullptr, tick_n_times, &data);
    }
    
    // Wait for all
    for (int i = 0; i < NUM_TEST_THREADS; i++) {
        pthread_join(threads[(size_t)i], nullptr);
    }
    
    // Check for collisions
    std::set<uint32_t> unique_indices(indices.begin(), indices.end());
    
    EXPECT_EQ(unique_indices.size(), (size_t)NUM_TEST_THREADS)
        << "TLS should give each thread a unique index (zero collisions)";
    
    std::cout << "\n[TLS SUCCESS]\n";
    std::cout << "  Tested threads: " << NUM_TEST_THREADS << "\n";
    std::cout << "  Unique indices: " << unique_indices.size() << "\n";
    std::cout << "  ✅ Zero collisions!\n";
}

struct ConcurrentTickData {
    vector_clock_t *vc;
    uint64_t *clock_snapshot;
    uint32_t *out_idx;
};

static void* tick_and_snapshot(void *arg) {
    ConcurrentTickData *data = (ConcurrentTickData*)arg;
    pthread_t self = pthread_self();
    
    vclock_tick(data->vc, self);
    vclock_snapshot(data->vc, data->clock_snapshot);
    *data->out_idx = vclock_get_thread_idx(data->vc, self);
    
    return nullptr;
}

TEST_F(VectorClockTest, TLSPreventsCollisions) {
    // Test: TLS ensures different threads get different slots
    // Requirement: No false causality from collisions!
    
    uint64_t clock_t1[MAX_THREADS], clock_t2[MAX_THREADS];
    uint32_t idx1, idx2;
    
    ConcurrentTickData data1 = {vc, clock_t1, &idx1};
    ConcurrentTickData data2 = {vc, clock_t2, &idx2};
    
    pthread_t thread_1, thread_2;
    pthread_create(&thread_1, nullptr, tick_and_snapshot, &data1);
    pthread_create(&thread_2, nullptr, tick_and_snapshot, &data2);
    
    pthread_join(thread_1, nullptr);
    pthread_join(thread_2, nullptr);
    
    // With TLS: Different threads → different indices
    EXPECT_NE(idx1, idx2) << "TLS should prevent collisions";
    
    // No false causality: Concurrent events should not have happens-before
    EXPECT_FALSE(vclock_happens_before(clock_t1, clock_t2, MAX_THREADS))
        << "Concurrent operations should not show causality";
    EXPECT_FALSE(vclock_happens_before(clock_t2, clock_t1, MAX_THREADS))
        << "Concurrent operations should not show causality (reverse)";
    
    std::cout << "\n[TLS PREVENTS FALSE CAUSALITY]\n";
    std::cout << "  Thread 1 → idx " << idx1 << "\n";
    std::cout << "  Thread 2 → idx " << idx2 << "\n";
    std::cout << "  Different slots → correct concurrent semantics!\n";
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(VectorClockTest, HandlesMaxThreads) {
    // Test: Can handle many concurrent threads
    // Note: With TLS-based implementation, threads must actually call pthread_self()
    // so we can't test full MAX_THREADS without creating real threads
    
    // Tick from this thread multiple times
    for (int i = 0; i < 100; i++) {
        vclock_tick(vc, pthread_self());
    }
    
    uint64_t *snapshot = new uint64_t[vc->max_threads];
    vclock_snapshot(vc, snapshot);
    
    // This thread should have clock = 100
    uint32_t idx = vclock_get_thread_idx(vc, pthread_self());
    EXPECT_EQ(snapshot[idx], 100);
    
    // Only 1 thread registered
    EXPECT_EQ(vc->num_registered, 1u);
    
    delete[] snapshot;
}

TEST_F(VectorClockTest, MergeIsIdempotent) {
    // Test: Merging same clock multiple times is safe
    // Requirement: Coordination can happen multiple times
    
    pthread_t thread_1 = make_thread_id(1000);
    
    vclock_tick(vc, thread_1);
    uint64_t *clock = new uint64_t[vc->max_threads];
    vclock_snapshot(vc, clock);
    
    vclock_merge(vc, clock);
    vclock_merge(vc, clock);
    vclock_merge(vc, clock);
    
    uint64_t after_merge[MAX_THREADS];
    vclock_snapshot(vc, after_merge);
    
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        EXPECT_EQ(clock[i], after_merge[i]) << "Merge changed clock at idx " << i;
    }
}

TEST_F(VectorClockTest, HappensBeforeWithDifferentNumThreads) {
    // Test: happens_before works with partial clock ranges
    // Requirement: Early in test, not all thread slots are used
    
    pthread_t thread_1 = make_thread_id(1000);
    
    vclock_tick(vc, thread_1);
    uint64_t clock_a[MAX_THREADS];
    vclock_snapshot(vc, clock_a);
    
    vclock_tick(vc, thread_1);
    uint64_t clock_b[MAX_THREADS];
    vclock_snapshot(vc, clock_b);
    
    // Check with num_threads = 1 (only check first slot)
    EXPECT_TRUE(vclock_happens_before(clock_a, clock_b, 1))
        << "Should work with partial range";
    
    // Check with num_threads = MAX_THREADS (check all)
    EXPECT_TRUE(vclock_happens_before(clock_a, clock_b, MAX_THREADS))
        << "Should work with full range";
}

// ============================================================================
// THREAD SAFETY
// ============================================================================

struct ThreadTestData {
    vector_clock_t *vc;
    pthread_t tid;
    int num_ticks;
};

static void* tick_worker(void *arg) {
    ThreadTestData *data = (ThreadTestData*)arg;
    for (int i = 0; i < data->num_ticks; i++) {
        vclock_tick(data->vc, data->tid);
    }
    return nullptr;
}

TEST_F(VectorClockTest, ThreadSafeTicking) {
    // Test: Multiple threads can tick concurrently
    // Requirement: Vector clock must be thread-safe
    
    const int NUM_THREADS = 4;
    const int TICKS_PER_THREAD = 1000;
    
    pthread_t threads[NUM_THREADS];
    ThreadTestData data[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        data[i].vc = vc;
        data[i].tid = make_thread_id((uint64_t)(i + 1) * 1000);
        data[i].num_ticks = TICKS_PER_THREAD;
        pthread_create(&threads[i], nullptr, tick_worker, &data[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    uint64_t *snapshot = new uint64_t[vc->max_threads];
    vclock_snapshot(vc, snapshot);
    
    // Verify each thread's clock
    for (int i = 0; i < NUM_THREADS; i++) {
        uint32_t idx = vclock_get_thread_idx(vc, data[i].tid);
        EXPECT_EQ(snapshot[idx], TICKS_PER_THREAD)
            << "Thread " << i << " clock incorrect";
    }
}

// ============================================================================
// PERFORMANCE
// ============================================================================

TEST_F(VectorClockTest, TickPerformance) {
    // Test: Measure tick performance (for benchmarking)
    // Requirement: Ticks happen on EVERY operation - must be fast!
    
    pthread_t tid = make_thread_id(1000);
    const int NUM_TICKS = 1000000;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < NUM_TICKS; i++) {
        vclock_tick(vc, tid);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    uint64_t ns = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000000UL +
                  (uint64_t)(end.tv_nsec - start.tv_nsec);
    double ns_per_tick = (double)ns / (double)NUM_TICKS;
    double ms_total = (double)ns / 1000000.0;
    
    std::cout << "\n[PERFORMANCE]\n";
    std::cout << "  Ticks: " << NUM_TICKS << "\n";
    std::cout << "  Time: " << ms_total << " ms\n";
    std::cout << "  Per tick: " << ns_per_tick << " ns\n";
    
    // Should be < 1000 ns per tick (1 microsecond)
    EXPECT_LT(ns_per_tick, 1000.0)
        << "Tick too slow (> 1µs), will impact performance";
}

