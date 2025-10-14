/*
 * Unit tests for eBPF programs using BPF_PROG_TEST_RUN
 * 
 * Tests eBPF programs in userspace by:
 * 1. Loading compiled .bpf.o files
 * 2. Running them with mock context data
 * 3. Verifying behavior via maps and return values
 * 
 * This tests the EXISTING pause_injector.bpf.c (which compiles).
 * Once rename_window.bpf.c and transaction_abort.bpf.c compile,
 * we'll add tests for them too.
 */

#include <gtest/gtest.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <linux/bpf.h>
#include <cstring>
#include <cstdint>

// Suppress libbpf debug output during tests
static int libbpf_print_fn(enum libbpf_print_level level, 
                           const char *format, va_list args) {
    if (level == LIBBPF_DEBUG) return 0;
    return vfprintf(stderr, format, args);
}

class BPFProgramTest : public ::testing::Test {
protected:
    void SetUp() override {
        libbpf_set_print(libbpf_print_fn);
    }
};

// ============================================================================
// Helper: Find BPF object file path
// ============================================================================

static std::string find_bpf_object(const char* filename) {
    // Try multiple possible locations
    std::string paths[] = {
        // Bazel runfiles path (injectors)
        std::string("bazel-bin/chaos/injectors/") + filename,
        // Bazel runfiles path (chaos root for legacy)
        std::string("bazel-bin/chaos/") + filename,
        // Relative to workspace (injectors)
        std::string("chaos/injectors/") + filename,
        // Relative to workspace (chaos root)
        std::string("chaos/") + filename,
        // Test data directory
        filename,
        ""
    };
    
    for (const auto& path : paths) {
        if (path.empty()) break;
        if (access(path.c_str(), F_OK) == 0) {
            return path;
        }
    }
    
    return filename;  // Return as-is and let it fail with clear error
}

// ============================================================================
// Test: pause_injector.bpf.c opens successfully (no kernel perms needed)
// ============================================================================

TEST_F(BPFProgramTest, PauseInjector_OpensSuccessfully) {
    std::string path = find_bpf_object("pause_injector.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    EXPECT_NE(obj, nullptr) << "Failed to open BPF object: " << path;
    
    if (obj) {
        // Verify we can find programs
        struct bpf_program *prog;
        int prog_count = 0;
        bpf_object__for_each_program(prog, obj) {
            prog_count++;
            EXPECT_NE(bpf_program__name(prog), nullptr);
        }
        EXPECT_GT(prog_count, 0) << "BPF object should contain at least one program";
        
        bpf_object__close(obj);
    }
}

// ============================================================================
// Test: pause_injector maps are defined (validation only, no kernel load)
// ============================================================================

TEST_F(BPFProgramTest, PauseInjector_MapsDefinedInObject) {
    std::string path = find_bpf_object("pause_injector.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    // Find config map (should exist even without loading)
    struct bpf_map *config_map = bpf_object__find_map_by_name(obj, "config");
    EXPECT_NE(config_map, nullptr) << "Config map not found in BPF object";
    
    if (config_map) {
        // Verify map properties without loading into kernel
        EXPECT_STREQ(bpf_map__name(config_map), "config");
        EXPECT_GT(bpf_map__max_entries(config_map), 0u);
    }
    
    // Find stats map
    struct bpf_map *stats_map = bpf_object__find_map_by_name(obj, "stats");
    EXPECT_NE(stats_map, nullptr) << "Stats map not found in BPF object";
    
    if (stats_map) {
        EXPECT_STREQ(bpf_map__name(stats_map), "stats");
        EXPECT_GT(bpf_map__max_entries(stats_map), 0u);
    }
    
    bpf_object__close(obj);
}

// ============================================================================
// Test: Map structure is correct (no kernel load needed)
// ============================================================================

TEST_F(BPFProgramTest, PauseInjector_MapStructure) {
    std::string path = find_bpf_object("pause_injector.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    struct bpf_map *config_map = bpf_object__find_map_by_name(obj, "config");
    ASSERT_NE(config_map, nullptr);
    
    // Verify map structure
    EXPECT_EQ(bpf_map__type(config_map), BPF_MAP_TYPE_ARRAY) 
        << "Config should be ARRAY type";
    EXPECT_GE(bpf_map__max_entries(config_map), 1u)
        << "Config should have at least 1 entry";
    EXPECT_EQ(bpf_map__key_size(config_map), sizeof(__u32));
    EXPECT_EQ(bpf_map__value_size(config_map), sizeof(__u32));
    
    struct bpf_map *stats_map = bpf_object__find_map_by_name(obj, "stats");
    ASSERT_NE(stats_map, nullptr);
    
    EXPECT_EQ(bpf_map__type(stats_map), BPF_MAP_TYPE_ARRAY)
        << "Stats should be ARRAY type";
    EXPECT_GE(bpf_map__max_entries(stats_map), 1u)
        << "Stats should have at least 1 entry";
    EXPECT_EQ(bpf_map__key_size(stats_map), sizeof(__u32));
    EXPECT_EQ(bpf_map__value_size(stats_map), sizeof(__u64));
    
    bpf_object__close(obj);
}

// ============================================================================
// Test: Stats map structure (no kernel load needed)
// ============================================================================

TEST_F(BPFProgramTest, PauseInjector_StatsMapStructure) {
    std::string path = find_bpf_object("pause_injector.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    struct bpf_map *stats_map = bpf_object__find_map_by_name(obj, "stats");
    ASSERT_NE(stats_map, nullptr);
    
    // Verify stats map has correct structure for our expected stats
    __u32 max_entries = bpf_map__max_entries(stats_map);
    
    // pause_injector uses a single-entry map (stores all stats in one struct)
    // Future injectors may use array-based stats
    EXPECT_GE(max_entries, 1u) << "Stats map should have at least 1 entry";
    
    bpf_object__close(obj);
}

// ============================================================================
// Test: Program can be found by name (no kernel load needed)
// ============================================================================

TEST_F(BPFProgramTest, PauseInjector_ProgramDefined) {
    std::string path = find_bpf_object("pause_injector.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    // Find program by name (works without loading)
    struct bpf_program *prog = bpf_object__find_program_by_name(
        obj, "trace_getdents64"
    );
    EXPECT_NE(prog, nullptr) << "trace_getdents64 program not found";
    
    if (prog) {
        // Verify program metadata (available before loading)
        const char *name = bpf_program__name(prog);
        EXPECT_STREQ(name, "trace_getdents64");
        
        // Section name should indicate tracepoint
        const char *section = bpf_program__section_name(prog);
        EXPECT_NE(section, nullptr);
        EXPECT_TRUE(strstr(section, "tracepoint") != nullptr)
            << "Section should be tracepoint, got: " << section;
    }
    
    bpf_object__close(obj);
}

// ============================================================================
// Test: BPF_PROG_TEST_RUN with mock context (requires CAP_BPF)
// ============================================================================

TEST_F(BPFProgramTest, DISABLED_PauseInjector_TestRunWithMockContext) {
    // NOTE: This test requires CAP_BPF or root to load BPF programs
    // It's DISABLED by default. To run: --gtest_also_run_disabled_tests
    // Or run with: sudo bazel test //chaos/injectors:bpf_test
    
    std::string path = find_bpf_object("pause_injector.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    int err = bpf_object__load(obj);
    if (err != 0) {
        bpf_object__close(obj);
        GTEST_SKIP() << "Requires CAP_BPF to load BPF programs. "
                     << "Run with sudo or --gtest_also_run_disabled_tests";
    }
    
    struct bpf_program *prog = bpf_object__find_program_by_name(
        obj, "trace_getdents64"
    );
    ASSERT_NE(prog, nullptr);
    
    int prog_fd = bpf_program__fd(prog);
    ASSERT_GT(prog_fd, 0);
    
    // Configure: 100% probability for deterministic test
    struct bpf_map *config_map = bpf_object__find_map_by_name(obj, "config");
    ASSERT_NE(config_map, nullptr);
    int config_fd = bpf_map__fd(config_map);
    
    __u32 key = 0, value = 100;  // 100% probability
    bpf_map_update_elem(config_fd, &key, &value, BPF_ANY);
    
    // Prepare mock tracepoint context
    char ctx[256] = {0};
    
    // Test run options
    struct bpf_test_run_opts opts = {};
    opts.sz = sizeof(opts);
    opts.ctx_in = ctx;
    opts.ctx_size_in = sizeof(ctx);
    opts.repeat = 1;
    
    // Run program
    err = bpf_prog_test_run_opts(prog_fd, &opts);
    
    if (err == 0) {
        EXPECT_EQ(opts.retval, 0) << "Program should return 0";
        EXPECT_GT(opts.duration, 0u) << "Should have non-zero duration";
        
        std::cout << "✅ BPF_PROG_TEST_RUN succeeded!" << std::endl;
        std::cout << "   Return value: " << opts.retval << std::endl;
        std::cout << "   Duration: " << opts.duration << " ns" << std::endl;
    } else if (err == -EOPNOTSUPP || err == -95) {
        GTEST_SKIP() << "BPF_PROG_TEST_RUN not supported for tracepoints";
    } else {
        FAIL() << "BPF_PROG_TEST_RUN failed: " << strerror(-err);
    }
    
    bpf_object__close(obj);
}

// ============================================================================
// Test: Multiple objects can be opened (no kernel load)
// ============================================================================

TEST_F(BPFProgramTest, MultipleObjects_CanOpen) {
    std::string path = find_bpf_object("pause_injector.bpf.o");
    
    // Open same program twice
    struct bpf_object *obj1 = bpf_object__open_file(path.c_str(), nullptr);
    struct bpf_object *obj2 = bpf_object__open_file(path.c_str(), nullptr);
    
    ASSERT_NE(obj1, nullptr);
    ASSERT_NE(obj2, nullptr);
    
    // Both should have their own map definitions
    struct bpf_map *map1 = bpf_object__find_map_by_name(obj1, "config");
    struct bpf_map *map2 = bpf_object__find_map_by_name(obj2, "config");
    
    EXPECT_NE(map1, nullptr);
    EXPECT_NE(map2, nullptr);
    
    // Objects should be distinct
    EXPECT_NE(obj1, obj2);
    
    bpf_object__close(obj1);
    bpf_object__close(obj2);
}

// ============================================================================
// Tests for rename_tracepoint.bpf.c (NEW - WORKING!)
// ============================================================================

TEST_F(BPFProgramTest, RenameTracepoint_OpensSuccessfully) {
    std::string path = find_bpf_object("rename_tracepoint.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    EXPECT_NE(obj, nullptr) << "Failed to open rename_tracepoint.bpf.o";
    
    if (obj) {
        struct bpf_program *prog;
        int prog_count = 0;
        bpf_object__for_each_program(prog, obj) {
            prog_count++;
        }
        EXPECT_EQ(prog_count, 2) << "Should have 2 programs (rename_exit, unlink_exit)";
        bpf_object__close(obj);
    }
}

TEST_F(BPFProgramTest, RenameTracepoint_MapsDefinedInObject) {
    std::string path = find_bpf_object("rename_tracepoint.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    struct bpf_map *config_map = bpf_object__find_map_by_name(obj, "config");
    EXPECT_NE(config_map, nullptr) << "Config map not found";
    
    struct bpf_map *stats_map = bpf_object__find_map_by_name(obj, "stats");
    EXPECT_NE(stats_map, nullptr) << "Stats map not found";
    
    bpf_object__close(obj);
}

TEST_F(BPFProgramTest, RenameTracepoint_MapStructure) {
    std::string path = find_bpf_object("rename_tracepoint.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    struct bpf_map *config_map = bpf_object__find_map_by_name(obj, "config");
    ASSERT_NE(config_map, nullptr);
    
    EXPECT_EQ(bpf_map__type(config_map), BPF_MAP_TYPE_ARRAY);
    EXPECT_EQ(bpf_map__max_entries(config_map), 3u);
    EXPECT_EQ(bpf_map__key_size(config_map), sizeof(__u32));
    EXPECT_EQ(bpf_map__value_size(config_map), sizeof(__u32));
    
    struct bpf_map *stats_map = bpf_object__find_map_by_name(obj, "stats");
    ASSERT_NE(stats_map, nullptr);
    
    EXPECT_EQ(bpf_map__type(stats_map), BPF_MAP_TYPE_ARRAY);
    EXPECT_EQ(bpf_map__max_entries(stats_map), 6u);
    EXPECT_EQ(bpf_map__value_size(stats_map), sizeof(__u64));
    
    bpf_object__close(obj);
}

TEST_F(BPFProgramTest, RenameTracepoint_ProgramsDefined) {
    std::string path = find_bpf_object("rename_tracepoint.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    struct bpf_program *rename_prog = bpf_object__find_program_by_name(
        obj, "trace_rename_exit"
    );
    EXPECT_NE(rename_prog, nullptr) << "trace_rename_exit not found";
    
    struct bpf_program *unlink_prog = bpf_object__find_program_by_name(
        obj, "trace_unlink_exit"
    );
    EXPECT_NE(unlink_prog, nullptr) << "trace_unlink_exit not found";
    
    bpf_object__close(obj);
}

TEST_F(BPFProgramTest, RenameTracepoint_TracepointSections) {
    std::string path = find_bpf_object("rename_tracepoint.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    struct bpf_program *prog;
    bpf_object__for_each_program(prog, obj) {
        const char *section = bpf_program__section_name(prog);
        EXPECT_TRUE(strstr(section, "tracepoint") != nullptr)
            << "Section should be tracepoint: " << section;
    }
    
    bpf_object__close(obj);
}

// ============================================================================
// Tests for link_tracepoint.bpf.c (NEW - WORKING!)
// ============================================================================

TEST_F(BPFProgramTest, LinkTracepoint_OpensSuccessfully) {
    std::string path = find_bpf_object("link_tracepoint.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    EXPECT_NE(obj, nullptr) << "Failed to open link_tracepoint.bpf.o";
    
    if (obj) {
        struct bpf_program *prog;
        int prog_count = 0;
        bpf_object__for_each_program(prog, obj) {
            prog_count++;
        }
        EXPECT_EQ(prog_count, 2) << "Should have 2 programs (link_exit, unlink_exit)";
        bpf_object__close(obj);
    }
}

TEST_F(BPFProgramTest, LinkTracepoint_MapsDefinedInObject) {
    std::string path = find_bpf_object("link_tracepoint.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    struct bpf_map *config_map = bpf_object__find_map_by_name(obj, "config");
    EXPECT_NE(config_map, nullptr) << "Config map not found";
    
    struct bpf_map *stats_map = bpf_object__find_map_by_name(obj, "stats");
    EXPECT_NE(stats_map, nullptr) << "Stats map not found";
    
    bpf_object__close(obj);
}

TEST_F(BPFProgramTest, LinkTracepoint_MapStructure) {
    std::string path = find_bpf_object("link_tracepoint.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    struct bpf_map *config_map = bpf_object__find_map_by_name(obj, "config");
    ASSERT_NE(config_map, nullptr);
    
    EXPECT_EQ(bpf_map__max_entries(config_map), 3u);
    EXPECT_EQ(bpf_map__key_size(config_map), sizeof(__u32));
    EXPECT_EQ(bpf_map__value_size(config_map), sizeof(__u32));
    
    struct bpf_map *stats_map = bpf_object__find_map_by_name(obj, "stats");
    ASSERT_NE(stats_map, nullptr);
    
    EXPECT_EQ(bpf_map__max_entries(stats_map), 6u);
    EXPECT_EQ(bpf_map__value_size(stats_map), sizeof(__u64));
    
    bpf_object__close(obj);
}

TEST_F(BPFProgramTest, LinkTracepoint_ProgramsDefined) {
    std::string path = find_bpf_object("link_tracepoint.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    struct bpf_program *link_prog = bpf_object__find_program_by_name(
        obj, "trace_link_exit"
    );
    EXPECT_NE(link_prog, nullptr) << "trace_link_exit not found";
    
    struct bpf_program *unlink_prog = bpf_object__find_program_by_name(
        obj, "trace_unlink_exit"
    );
    EXPECT_NE(unlink_prog, nullptr) << "trace_unlink_exit not found";
    
    bpf_object__close(obj);
}

TEST_F(BPFProgramTest, LinkTracepoint_TracepointSections) {
    std::string path = find_bpf_object("link_tracepoint.bpf.o");
    
    struct bpf_object *obj = bpf_object__open_file(path.c_str(), nullptr);
    ASSERT_NE(obj, nullptr);
    
    struct bpf_program *prog;
    bpf_object__for_each_program(prog, obj) {
        const char *section = bpf_program__section_name(prog);
        EXPECT_TRUE(strstr(section, "tracepoint") != nullptr)
            << "Section should be tracepoint: " << section;
    }
    
    bpf_object__close(obj);
}

