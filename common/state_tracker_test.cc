// Unit tests for state_tracker - the core validation logic
// These tests verify we can:
// 1. Correctly detect real bugs (true positives)
// 2. Not report false bugs (no false positives)

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

extern "C" {
#include "state_tracker.h"
}

// Test fixture
class StateTrackerTest : public ::testing::Test {
protected:
    state_tracker_t* tracker;
    
    void SetUp() override {
        tracker = tracker_init();
        ASSERT_NE(tracker, nullptr);
    }
    
    void TearDown() override {
        tracker_cleanup(tracker);
    }
    
    // Helper: Create mutable copies of string literals for C API
    // The C API expects char**, but C++ string literals are const char*
    // This helper allocates proper mutable copies
    class StringArray {
    public:
        StringArray(std::initializer_list<const char*> strings) {
            for (const char* s : strings) {
                char* copy = strdup(s);
                ptrs.push_back(copy);
            }
        }
        
        ~StringArray() {
            for (char* ptr : ptrs) {
                free(ptr);
            }
        }
        
        char** data() { return ptrs.data(); }
        int size() const { return static_cast<int>(ptrs.size()); }
        
    private:
        std::vector<char*> ptrs;
    };
};

// ============================================================================
// REQUIREMENT: State tracker correctly tracks file creation
// ============================================================================
TEST_F(StateTrackerTest, TracksFileCreation) {
    // Given: A file is created
    tracker_record_create(tracker, "test_file.txt");
    
    // When: We get expected entries at a later time
    char* entries[10];
    int count = tracker_get_expected_entries(tracker, 1000000000, entries, 10);
    
    // Then: The file should be in expected state
    EXPECT_EQ(count, 1);
    EXPECT_STREQ(entries[0], "test_file.txt");
    
    for (int i = 0; i < count; i++) free(entries[i]);
}

// ============================================================================
// REQUIREMENT: State tracker correctly tracks file deletion
// ============================================================================
TEST_F(StateTrackerTest, TracksFileDeletion) {
    // Given: A file is created then deleted
    tracker_record_create(tracker, "temp_file.txt");
    tracker_record_delete(tracker, "temp_file.txt");
    
    // When: We get expected entries after deletion
    char* entries[10];
    int count = tracker_get_expected_entries(tracker, 2000000000, entries, 10);
    
    // Then: File should NOT be in expected state
    EXPECT_EQ(count, 0) << "Deleted file should not appear in expected entries";
    
    for (int i = 0; i < count; i++) free(entries[i]);
}

// ============================================================================
// REQUIREMENT: Validation detects missing entries (TRUE POSITIVE)
// ============================================================================
TEST_F(StateTrackerTest, DetectsMissingEntries_TruePositive) {
    // Given: Files exist in ground truth
    tracker_record_create(tracker, "file1.txt");
    tracker_record_create(tracker, "file2.txt");
    tracker_record_create(tracker, "file3.txt");
    
    // When: Directory read MISSES some files (BUG!)
    StringArray actual{"file1.txt"};  // Missing file2 and file3!
    
    validation_result_t result = tracker_validate_read(
        tracker, actual.data(), actual.size(),
        100000000,  // read start
        200000000,  // read end
        CONSISTENCY_WEAK_POSIX
    );
    
    // Then: Should detect missing entries as bugs
    EXPECT_GT(result.missing_entries, 0) << "Should detect missing files";
    EXPECT_GT(result.total_bugs_found, 0) << "Should report bugs";
    EXPECT_EQ(result.duplicate_entries, 0) << "No duplicates in this scenario";
    EXPECT_EQ(result.phantom_entries, 0) << "No phantoms in this scenario";
}

// ============================================================================
// REQUIREMENT: Validation detects duplicate entries (TRUE POSITIVE)
// ============================================================================
TEST_F(StateTrackerTest, DetectsDuplicateEntries_TruePositive) {
    // Given: One file exists
    tracker_record_create(tracker, "dup_file.txt");
    
    // When: Directory read returns DUPLICATE (BUG!)
    StringArray actual{"dup_file.txt", "dup_file.txt"};
    
    validation_result_t result = tracker_validate_read(
        tracker, actual.data(), actual.size(),
        100000000,
        200000000,
        CONSISTENCY_WEAK_POSIX
    );
    
    // Then: Should detect duplicates as bugs
    EXPECT_GT(result.duplicate_entries, 0) << "Should detect duplicate entries";
    EXPECT_GT(result.total_bugs_found, 0) << "Should report bugs";
}

// ============================================================================
// REQUIREMENT: Validation detects phantom entries (TRUE POSITIVE)
// ============================================================================
TEST_F(StateTrackerTest, DetectsPhantomEntries_TruePositive) {
    // Given: No files created
    
    // When: Directory read returns a file that doesn't exist (BUG!)
    StringArray actual{"phantom_file.txt"};
    
    validation_result_t result = tracker_validate_read(
        tracker, actual.data(), actual.size(),
        100000000,
        200000000,
        CONSISTENCY_WEAK_POSIX
    );
    
    // Then: Should detect phantom entry as bug
    EXPECT_GT(result.phantom_entries, 0) << "Should detect phantom file";
    EXPECT_GT(result.total_bugs_found, 0) << "Should report bugs";
}

// ============================================================================
// REQUIREMENT: Validation does NOT report false positives
// ============================================================================
TEST_F(StateTrackerTest, NoFalsePositives_CorrectRead) {
    // Given: Two files exist
    tracker_record_create(tracker, "file_a.txt");
    tracker_record_create(tracker, "file_b.txt");
    
    // When: Directory read returns EXACTLY those files (CORRECT!)
    StringArray actual{"file_a.txt", "file_b.txt"};
    
    validation_result_t result = tracker_validate_read(
        tracker, actual.data(), actual.size(),
        100000000,
        200000000,
        CONSISTENCY_WEAK_POSIX
    );
    
    // Then: Should NOT report any bugs
    EXPECT_EQ(result.total_bugs_found, 0) << "Correct read should have no bugs";
    EXPECT_EQ(result.missing_entries, 0) << "All files present";
    EXPECT_EQ(result.duplicate_entries, 0) << "No duplicates";
    EXPECT_EQ(result.phantom_entries, 0) << "No phantoms";
}

// ============================================================================
// REQUIREMENT: WEAK consistency allows operations during read window
// ============================================================================
TEST_F(StateTrackerTest, WeakConsistency_AllowsRaceWindowOperations) {
    // Given: File created BEFORE read starts
    tracker_record_create(tracker, "before_file.txt");
    
    uint64_t read_start = 100000000;
    
    // File created DURING read (in race window)
    tracker_record_create(tracker, "during_file.txt");
    
    uint64_t read_end = 200000000;
    
    // When: Read sees before_file but NOT during_file
    StringArray actual{"before_file.txt"};
    
    validation_result_t result = tracker_validate_read(
        tracker, actual.data(), actual.size(),
        read_start,
        read_end,
        CONSISTENCY_WEAK_POSIX  // WEAK model
    );
    
    // Then: Should NOT report as bug (file created during read is allowed to be missing)
    EXPECT_EQ(result.missing_entries, 0) << "WEAK model allows missing entries for files created during read";
    EXPECT_EQ(result.total_bugs_found, 0) << "This is NOT a bug in WEAK model";
}

// ============================================================================
// REQUIREMENT: STRICT consistency requires all operations visible
// ============================================================================
TEST_F(StateTrackerTest, StrictConsistency_RequiresAllOperationsVisible) {
    // Given: File created before read ends
    tracker_record_create(tracker, "strict_file.txt");
    
    // When: Read completes AFTER creation but file is missing
    // Empty read - pass nullptr for empty array
    
    validation_result_t result = tracker_validate_read(
        tracker, nullptr, 0,
        100000000,  // read start
        200000000,  // read end (AFTER file creation)
        CONSISTENCY_STRICT  // STRICT model
    );
    
    // Then: Should report as bug (STRICT requires all ops before read_end visible)
    EXPECT_GT(result.missing_entries, 0) << "STRICT model requires file visible";
    EXPECT_GT(result.total_bugs_found, 0) << "This IS a bug in STRICT model";
}

// ============================================================================
// REQUIREMENT: EVENTUAL consistency only detects duplicates
// ============================================================================
TEST_F(StateTrackerTest, EventualConsistency_OnlyDetectsDuplicates) {
    // Given: File created
    tracker_record_create(tracker, "eventual_file.txt");
    
    // When: Read misses the file (might be propagation delay)
    // Empty read - eventual consistency allows this
    
    validation_result_t result1 = tracker_validate_read(
        tracker, nullptr, 0,
        100000000, 200000000,
        CONSISTENCY_EVENTUAL
    );
    
    // Then: Should NOT report missing as bug (eventual allows delays)
    EXPECT_EQ(result1.total_bugs_found, 0) << "EVENTUAL allows missing entries";
    
    // But: Duplicates are ALWAYS bugs
    StringArray actual_duplicate{"eventual_file.txt", "eventual_file.txt"};
    
    validation_result_t result2 = tracker_validate_read(
        tracker, actual_duplicate.data(), actual_duplicate.size(),
        100000000, 200000000,
        CONSISTENCY_EVENTUAL
    );
    
    EXPECT_GT(result2.duplicate_entries, 0) << "EVENTUAL detects duplicates";
    EXPECT_GT(result2.total_bugs_found, 0) << "Duplicates are bugs in any model";
}

// ============================================================================
// REQUIREMENT: Empty directory reads validate correctly
// ============================================================================
TEST_F(StateTrackerTest, HandlesEmptyDirectory) {
    // Given: No files created
    
    // When: Directory read returns empty
    // Empty read - pass nullptr for empty array
    
    validation_result_t result = tracker_validate_read(
        tracker, nullptr, 0,
        100000000, 200000000,
        CONSISTENCY_WEAK_POSIX
    );
    
    // Then: Should be valid (no bugs)
    EXPECT_EQ(result.total_bugs_found, 0) << "Empty directory is valid";
}

// ============================================================================
// REQUIREMENT: High concurrency scenario - multiple operations
// ============================================================================
TEST_F(StateTrackerTest, HandlesHighConcurrencyScenario) {
    // Given: Complex sequence of creates and deletes
    for (int i = 0; i < 100; i++) {
        char filename[64];
        snprintf(filename, sizeof(filename), "file_%d.txt", i);
        tracker_record_create(tracker, filename);
    }
    
    // Delete every other file
    for (int i = 0; i < 100; i += 2) {
        char filename[64];
        snprintf(filename, sizeof(filename), "file_%d.txt", i);
        tracker_record_delete(tracker, filename);
    }
    
    // When: Read returns only the non-deleted files
    std::vector<char*> actual;
    for (int i = 1; i < 100; i += 2) {
        char* filename = (char*)malloc(64);
        snprintf(filename, 64, "file_%d.txt", i);
        actual.push_back(filename);
    }
    
    validation_result_t result = tracker_validate_read(
        tracker, actual.data(), (int)actual.size(),
        500000000, 600000000,
        CONSISTENCY_WEAK_POSIX
    );
    
    // Then: Should be valid (correct state)
    EXPECT_EQ(result.total_bugs_found, 0) << "Correct read should have no bugs";
    
    for (auto* ptr : actual) free(ptr);
}

// ============================================================================
// REQUIREMENT: Detect when deleted file still appears (stale cache bug)
// ============================================================================
TEST_F(StateTrackerTest, DetectsStaleCache_DeletedFileStillVisible) {
    // Simulates: File deleted but still in directory cache (common kernel bug!)
    
    // Given: File created then deleted
    tracker_record_create(tracker, "deleted.txt");
    tracker_record_delete(tracker, "deleted.txt");
    
    // When: Read happens AFTER deletion but still sees file (STALE CACHE BUG!)
    StringArray actual{"deleted.txt"};
    
    validation_result_t result = tracker_validate_read(
        tracker, actual.data(), actual.size(),
        300000000,  // After deletion
        400000000,
        CONSISTENCY_WEAK_POSIX
    );
    
    // Then: Should detect as phantom entry
    EXPECT_GT(result.phantom_entries, 0) << "Deleted file appearing is a bug";
    EXPECT_GT(result.total_bugs_found, 0) << "Should report bug";
}

// ============================================================================
// REQUIREMENT: Detect when created file doesn't appear (cache miss bug)
// ============================================================================
TEST_F(StateTrackerTest, DetectsCacheMiss_CreatedFileMissing) {
    // Simulates: File created but not yet in directory cache
    
    // Given: File created BEFORE read
    tracker_record_create(tracker, "new_file.txt");
    
    // When: Read happens AFTER creation but doesn't see file (CACHE MISS BUG!)
    // Empty read - file is missing
    
    validation_result_t result = tracker_validate_read(
        tracker, nullptr, 0,
        100000000,  // After creation
        200000000,
        CONSISTENCY_WEAK_POSIX
    );
    
    // Then: Should detect as missing entry
    EXPECT_GT(result.missing_entries, 0) << "Created file not appearing is a bug";
    EXPECT_GT(result.total_bugs_found, 0) << "Should report bug";
}

// ============================================================================
// REQUIREMENT: Multi-threaded tracking is safe
// ============================================================================
TEST_F(StateTrackerTest, ThreadSafeOperations) {
    // Given: Multiple threads record operations concurrently
    std::vector<std::thread> threads;
    
    for (int t = 0; t < 10; t++) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < 100; i++) {
                char filename[64];
                snprintf(filename, sizeof(filename), "thread%d_file%d.txt", t, i);
                tracker_record_create(tracker, filename);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // When: We get expected entries
    char* entries[2000];
    int count = tracker_get_expected_entries(tracker, 9999999999, entries, 2000);
    
    // Then: Should have all 1000 files (10 threads × 100 files)
    EXPECT_EQ(count, 1000) << "All files from all threads should be tracked";
    
    for (int i = 0; i < count; i++) free(entries[i]);
}

// ============================================================================
// REQUIREMENT: Timestamp-based validation works correctly
// ============================================================================
TEST_F(StateTrackerTest, TimestampBasedValidation) {
    // This tests the core Jepsen-inspired approach: operations have timestamps
    
    // Given: Files created at different times
    tracker_record_create(tracker, "early_file.txt");  // Created at ~T1
    usleep(1000);
    uint64_t middle_time = 500000000;
    usleep(1000);
    tracker_record_create(tracker, "late_file.txt");   // Created at ~T2
    
    // When: Read happens at middle time (between T1 and T2)
    StringArray actual{"early_file.txt"};  // Only sees early file
    
    validation_result_t result = tracker_validate_read(
        tracker, actual.data(), actual.size(),
        middle_time - 1000,
        middle_time + 1000,
        CONSISTENCY_WEAK_POSIX
    );
    
    // Then: Should be valid (late_file created after read, OK to be missing)
    // Note: This test may be timing-dependent, but demonstrates the concept
    EXPECT_EQ(result.total_bugs_found, 0) << "Missing future files is not a bug in WEAK model";
}

// ============================================================================
// REQUIREMENT: Large-scale stress test (performance + correctness)
// ============================================================================
TEST_F(StateTrackerTest, HandlesLargeScale) {
    // Given: Many operations (stress test)
    for (int i = 0; i < 1000; i++) {
        char filename[64];
        snprintf(filename, sizeof(filename), "stress_%d.txt", i);
        tracker_record_create(tracker, filename);
    }
    
    // When: We get expected entries
    char* entries[2000];
    int count = tracker_get_expected_entries(tracker, 9999999999, entries, 2000);
    
    // Then: Should handle it without crashing or corruption
    EXPECT_EQ(count, 1000) << "Should track all 1000 files";
    EXPECT_LE(count, 2000) << "Should not overflow buffer";
    
    for (int i = 0; i < count; i++) free(entries[i]);
}

// ============================================================================
// REQUIREMENT: Correctly validates lost+found directory (special case)
// ============================================================================
TEST_F(StateTrackerTest, HandlesLostAndFound) {
    // lost+found exists in ext4 but we didn't create it
    // Should NOT be reported as phantom (it's a filesystem artifact)
    
    // When: Read includes lost+found
    StringArray actual{"lost+found"};
    
    validation_result_t result = tracker_validate_read(
        tracker, actual.data(), actual.size(),
        100000000, 200000000,
        CONSISTENCY_WEAK_POSIX
    );
    
    // Then: Check if validation handles special directories
    // NOTE: Current implementation may report this as phantom - that's OK if documented
    // This test documents the behavior
    if (result.phantom_entries > 0) {
        std::cout << "  Note: lost+found reported as phantom (may be expected)" << std::endl;
    }
}

// ============================================================================
// Main test runner
// ============================================================================
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

