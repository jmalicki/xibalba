/*
 * Unit tests for workload modules
 * 
 * Tests verify that:
 * 1. Workload registry functions correctly
 * 2. Workload initialization/cleanup works
 * 3. Default thread counts are reasonable
 * 4. Statistics collection works
 */

#include <gtest/gtest.h>
#include <string>
#include <cstring>

extern "C" {
#include "workload.h"
}

// ============================================================================
// Test Fixture
// ============================================================================

class WorkloadTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test directory
        char temp_template[] = "/tmp/xibalba_test_XXXXXX";
        test_dir = mkdtemp(temp_template);
        ASSERT_NE(test_dir, nullptr) << "Failed to create temp directory";
    }
    
    void TearDown() override {
        // Cleanup test directory
        if (test_dir) {
            std::string cmd = "rm -rf ";
            cmd += test_dir;
            int ret = system(cmd.c_str());
            (void)ret;  // Ignore errors in cleanup
        }
    }
    
    char* test_dir = nullptr;
};

// ============================================================================
// Registry Tests
// ============================================================================

TEST_F(WorkloadTest, GetWorkload_ValidNames) {
    // All workload names should be retrievable
    const char* valid_names[] = {
        "create_delete",
        "rename",
        "hardlink",
        "mixed"
    };
    
    for (const char* name : valid_names) {
        const workload_ops_t* wl = get_workload(name);
        ASSERT_NE(wl, nullptr) << "Failed to get workload: " << name;
        EXPECT_STREQ(wl->name, name);
        EXPECT_NE(wl->description, nullptr);
    }
}

TEST_F(WorkloadTest, GetWorkload_InvalidName) {
    const workload_ops_t* wl = get_workload("nonexistent_workload");
    EXPECT_EQ(wl, nullptr) << "Should return NULL for invalid workload";
}

TEST_F(WorkloadTest, GetWorkload_NullName) {
    const workload_ops_t* wl = get_workload(nullptr);
    EXPECT_EQ(wl, nullptr) << "Should return NULL for NULL name";
}

TEST_F(WorkloadTest, ListWorkloads_Succeeds) {
    // Just verify it doesn't crash
    list_workloads(stdout);
    list_workloads(stderr);
}

// ============================================================================
// Workload Interface Tests
// ============================================================================

TEST_F(WorkloadTest, CreateDelete_InterfaceComplete) {
    const workload_ops_t* wl = get_workload("create_delete");
    ASSERT_NE(wl, nullptr);
    
    // Verify all function pointers are set
    EXPECT_NE(wl->init, nullptr);
    EXPECT_NE(wl->reader_fn, nullptr);
    EXPECT_NE(wl->writer_fn, nullptr);
    EXPECT_NE(wl->cleanup, nullptr);
    EXPECT_NE(wl->get_default_readers, nullptr);
    EXPECT_NE(wl->get_default_writers, nullptr);
    EXPECT_NE(wl->get_stats, nullptr);
    
    // Verify metadata
    EXPECT_STREQ(wl->name, "create_delete");
    EXPECT_NE(wl->description, nullptr);
    EXPECT_GT(strlen(wl->description), 0);
}

TEST_F(WorkloadTest, Rename_InterfaceComplete) {
    const workload_ops_t* wl = get_workload("rename");
    ASSERT_NE(wl, nullptr);
    
    EXPECT_NE(wl->init, nullptr);
    EXPECT_NE(wl->reader_fn, nullptr);
    EXPECT_NE(wl->writer_fn, nullptr);
    EXPECT_NE(wl->cleanup, nullptr);
    EXPECT_STREQ(wl->name, "rename");
}

TEST_F(WorkloadTest, Hardlink_InterfaceComplete) {
    const workload_ops_t* wl = get_workload("hardlink");
    ASSERT_NE(wl, nullptr);
    
    EXPECT_NE(wl->init, nullptr);
    EXPECT_NE(wl->reader_fn, nullptr);
    EXPECT_NE(wl->writer_fn, nullptr);
    EXPECT_NE(wl->cleanup, nullptr);
    EXPECT_STREQ(wl->name, "hardlink");
}

TEST_F(WorkloadTest, Mixed_InterfaceComplete) {
    const workload_ops_t* wl = get_workload("mixed");
    ASSERT_NE(wl, nullptr);
    
    EXPECT_NE(wl->init, nullptr);
    EXPECT_NE(wl->reader_fn, nullptr);
    EXPECT_NE(wl->writer_fn, nullptr);
    EXPECT_NE(wl->cleanup, nullptr);
    EXPECT_STREQ(wl->name, "mixed");
}

// ============================================================================
// Initialization and Cleanup Tests
// ============================================================================

TEST_F(WorkloadTest, CreateDelete_InitCleanup) {
    const workload_ops_t* wl = get_workload("create_delete");
    ASSERT_NE(wl, nullptr);
    
    // Create dummy state
    workload_state_t state = {};
    state.test_dir = test_dir;
    
    // Initialize
    int ret = wl->init(&state, test_dir);
    EXPECT_EQ(ret, 0) << "Initialization should succeed";
    EXPECT_NE(state.workload_data, nullptr) << "Should allocate workload data";
    
    // Cleanup
    wl->cleanup(&state);
    // No crash = success
}

TEST_F(WorkloadTest, AllWorkloads_InitCleanup) {
    const char* workloads[] = {
        "create_delete", "rename", "hardlink", "mixed"
    };
    
    for (const char* name : workloads) {
        const workload_ops_t* wl = get_workload(name);
        ASSERT_NE(wl, nullptr) << "Workload not found: " << name;
        
        workload_state_t state = {};
        state.test_dir = test_dir;
        
        int ret = wl->init(&state, test_dir);
        EXPECT_EQ(ret, 0) << "Init failed for: " << name;
        
        wl->cleanup(&state);
        // Verify cleanup doesn't crash
    }
}

TEST_F(WorkloadTest, DoubleCleanup_NoCrash) {
    const workload_ops_t* wl = get_workload("create_delete");
    ASSERT_NE(wl, nullptr);
    
    workload_state_t state = {};
    state.test_dir = test_dir;
    
    wl->init(&state, test_dir);
    wl->cleanup(&state);
    wl->cleanup(&state);  // Should handle gracefully
}

// ============================================================================
// Default Thread Count Tests
// ============================================================================

TEST_F(WorkloadTest, CreateDelete_DefaultThreads) {
    const workload_ops_t* wl = get_workload("create_delete");
    ASSERT_NE(wl, nullptr);
    
    int readers = wl->get_default_readers();
    int writers = wl->get_default_writers();
    
    EXPECT_GT(readers, 0) << "Should have at least 1 reader";
    EXPECT_GT(writers, 0) << "Should have at least 1 writer";
    EXPECT_LT(readers, 100) << "Reader count seems unreasonable";
    EXPECT_LT(writers, 100) << "Writer count seems unreasonable";
}

TEST_F(WorkloadTest, AllWorkloads_ReasonableThreadCounts) {
    const char* workloads[] = {
        "create_delete", "rename", "hardlink", "mixed"
    };
    
    for (const char* name : workloads) {
        const workload_ops_t* wl = get_workload(name);
        ASSERT_NE(wl, nullptr);
        
        int readers = wl->get_default_readers();
        int writers = wl->get_default_writers();
        
        EXPECT_GT(readers, 0) << name << ": needs readers";
        EXPECT_GT(writers, 0) << name << ": needs writers";
        EXPECT_LT(readers, 100) << name << ": too many readers";
        EXPECT_LT(writers, 100) << name << ": too many writers";
        
        // Total threads should be reasonable
        int total = readers + writers;
        EXPECT_LT(total, 50) << name << ": total thread count too high";
    }
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(WorkloadTest, CreateDelete_GetStats) {
    const workload_ops_t* wl = get_workload("create_delete");
    ASSERT_NE(wl, nullptr);
    
    workload_state_t state = {};
    state.test_dir = test_dir;
    wl->init(&state, test_dir);
    
    char buf[512];
    wl->get_stats(&state, buf, sizeof(buf));
    
    // Should produce some output
    EXPECT_GT(strlen(buf), 0) << "Stats should produce output";
    
    wl->cleanup(&state);
}

TEST_F(WorkloadTest, AllWorkloads_GetStats) {
    const char* workloads[] = {
        "create_delete", "rename", "hardlink", "mixed"
    };
    
    for (const char* name : workloads) {
        const workload_ops_t* wl = get_workload(name);
        ASSERT_NE(wl, nullptr);
        
        workload_state_t state = {};
        state.test_dir = test_dir;
        wl->init(&state, test_dir);
        
        char buf[512];
        wl->get_stats(&state, buf, sizeof(buf));
        
        EXPECT_GT(strlen(buf), 0) << name << ": no stats output";
        
        wl->cleanup(&state);
    }
}

TEST_F(WorkloadTest, GetStats_BufferBoundary) {
    const workload_ops_t* wl = get_workload("create_delete");
    ASSERT_NE(wl, nullptr);
    
    workload_state_t state = {};
    state.test_dir = test_dir;
    wl->init(&state, test_dir);
    
    // Small buffer
    char small_buf[10];
    wl->get_stats(&state, small_buf, sizeof(small_buf));
    // Should not overflow (verified by sanitizers if enabled)
    
    // Large buffer
    char large_buf[4096];
    wl->get_stats(&state, large_buf, sizeof(large_buf));
    EXPECT_GT(strlen(large_buf), 0);
    
    wl->cleanup(&state);
}

// ============================================================================
// Workload-Specific Tests
// ============================================================================

TEST_F(WorkloadTest, Rename_HigherWriterCount) {
    // Rename workload should have more writers (it's write-heavy)
    const workload_ops_t* rename_wl = get_workload("rename");
    const workload_ops_t* create_del_wl = get_workload("create_delete");
    
    ASSERT_NE(rename_wl, nullptr);
    ASSERT_NE(create_del_wl, nullptr);
    
    int rename_writers = rename_wl->get_default_writers();
    int create_del_writers = create_del_wl->get_default_writers();
    
    // Rename workload should have at least as many writers
    EXPECT_GE(rename_writers, create_del_writers)
        << "Rename workload should be write-intensive";
}

TEST_F(WorkloadTest, Hardlink_BalancedThreads) {
    const workload_ops_t* wl = get_workload("hardlink");
    ASSERT_NE(wl, nullptr);
    
    int readers = wl->get_default_readers();
    int writers = wl->get_default_writers();
    
    // Hardlink workload needs good balance
    // Writers create links, readers validate ref counts
    float ratio = (float)writers / (float)readers;
    EXPECT_GT(ratio, 0.3) << "Should have substantial writer presence";
    EXPECT_LT(ratio, 3.0) << "Should not be too writer-heavy";
}

TEST_F(WorkloadTest, Mixed_HighestWriterCount) {
    // Mixed workload does everything, should have most writers
    const workload_ops_t* mixed_wl = get_workload("mixed");
    ASSERT_NE(mixed_wl, nullptr);
    
    int mixed_writers = mixed_wl->get_default_writers();
    
    // Should have substantial writer count for comprehensive testing
    EXPECT_GE(mixed_writers, 4)
        << "Mixed workload needs multiple writers for all operations";
}

// ============================================================================
// Description Quality Tests
// ============================================================================

TEST_F(WorkloadTest, AllWorkloads_DescriptionsAreDescriptive) {
    const char* workloads[] = {
        "create_delete", "rename", "hardlink", "mixed"
    };
    
    for (const char* name : workloads) {
        const workload_ops_t* wl = get_workload(name);
        ASSERT_NE(wl, nullptr);
        
        const char* desc = wl->description;
        ASSERT_NE(desc, nullptr);
        
        // Should be more than just the name
        EXPECT_GT(strlen(desc), strlen(name))
            << name << ": description too short";
        
        // Should contain useful information
        EXPECT_GT(strlen(desc), 20)
            << name << ": description should be informative";
    }
}

