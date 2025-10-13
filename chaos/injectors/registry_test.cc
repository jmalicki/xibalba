/*
 * Unit tests for injector registry
 * 
 * Tests verify that:
 * 1. Injector registry functions correctly
 * 2. All injectors are properly described
 * 3. Filesystem compatibility checks work
 * 4. Metadata is consistent
 */

#include <gtest/gtest.h>
#include <string>
#include <cstring>

extern "C" {
#include "injector.h"
}

// ============================================================================
// Registry Tests
// ============================================================================

TEST(InjectorRegistryTest, GetInjector_ValidNames) {
    const char* valid_names[] = {
        "getdents_delay",
        "rename_window",
        "transaction_abort",
        "vfs_delay",
        "btrfs_specific",
        "multi_hook"
    };
    
    for (const char* name : valid_names) {
        const injector_descriptor_t* inj = get_injector(name);
        ASSERT_NE(inj, nullptr) << "Failed to get injector: " << name;
        EXPECT_STREQ(inj->name, name);
        EXPECT_NE(inj->description, nullptr);
        EXPECT_NE(inj->bpf_object_filename, nullptr);
    }
}

TEST(InjectorRegistryTest, GetInjector_InvalidName) {
    const injector_descriptor_t* inj = get_injector("nonexistent_injector");
    EXPECT_EQ(inj, nullptr) << "Should return NULL for invalid injector";
}

TEST(InjectorRegistryTest, GetInjector_NullName) {
    const injector_descriptor_t* inj = get_injector(nullptr);
    EXPECT_EQ(inj, nullptr) << "Should return NULL for NULL name";
}

TEST(InjectorRegistryTest, ListInjectors_Succeeds) {
    // Just verify it doesn't crash
    list_injectors(stdout);
    list_injectors(stderr);
}

// ============================================================================
// Descriptor Completeness Tests
// ============================================================================

TEST(InjectorRegistryTest, AllInjectors_DescriptorsComplete) {
    const char* names[] = {
        "getdents_delay", "rename_window", "transaction_abort",
        "vfs_delay", "btrfs_specific", "multi_hook"
    };
    
    for (const char* name : names) {
        const injector_descriptor_t* inj = get_injector(name);
        ASSERT_NE(inj, nullptr) << "Injector not found: " << name;
        
        // Required fields
        EXPECT_NE(inj->name, nullptr) << name << ": missing name";
        EXPECT_NE(inj->description, nullptr) << name << ": missing description";
        EXPECT_NE(inj->bpf_object_filename, nullptr)
            << name << ": missing BPF filename";
        
        // At least one filesystem should be supported
        bool supports_any = inj->supports_generic ||
                           inj->supports_ext4 ||
                           inj->supports_xfs ||
                           inj->supports_btrfs ||
                           inj->supports_f2fs ||
                           inj->supports_bcachefs;
        EXPECT_TRUE(supports_any) << name << ": doesn't support any filesystem";
        
        // Should have at least one hook point
        EXPECT_GT(inj->num_hooks, 0) << name << ": no hook points defined";
        EXPECT_LE(inj->num_hooks, 20) << name << ": too many hook points";
        
        // Should target something
        EXPECT_GT(inj->num_targets, 0) << name << ": no targets defined";
        EXPECT_LE(inj->num_targets, 10) << name << ": too many targets";
    }
}

TEST(InjectorRegistryTest, BPFObjectFilenames_Valid) {
    const char* names[] = {
        "getdents_delay", "rename_window", "transaction_abort",
        "vfs_delay", "btrfs_specific", "multi_hook"
    };
    
    for (const char* name : names) {
        const injector_descriptor_t* inj = get_injector(name);
        ASSERT_NE(inj, nullptr);
        
        const char* filename = inj->bpf_object_filename;
        ASSERT_NE(filename, nullptr);
        
        // Should end with .bpf.o
        size_t len = strlen(filename);
        EXPECT_GT(len, 6) << name << ": filename too short";
        EXPECT_STREQ(filename + len - 6, ".bpf.o")
            << name << ": filename should end with .bpf.o";
    }
}

TEST(InjectorRegistryTest, HookPoints_AllValid) {
    const char* names[] = {
        "getdents_delay", "rename_window", "transaction_abort",
        "vfs_delay", "btrfs_specific", "multi_hook"
    };
    
    for (const char* name : names) {
        const injector_descriptor_t* inj = get_injector(name);
        ASSERT_NE(inj, nullptr);
        
        for (int i = 0; i < inj->num_hooks; i++) {
            ASSERT_NE(inj->hook_points[i], nullptr)
                << name << ": hook point " << i << " is NULL";
            EXPECT_GT(strlen(inj->hook_points[i]), 0)
                << name << ": hook point " << i << " is empty";
        }
    }
}

TEST(InjectorRegistryTest, Targets_AllValid) {
    const char* names[] = {
        "getdents_delay", "rename_window", "transaction_abort",
        "vfs_delay", "btrfs_specific", "multi_hook"
    };
    
    for (const char* name : names) {
        const injector_descriptor_t* inj = get_injector(name);
        ASSERT_NE(inj, nullptr);
        
        for (int i = 0; i < inj->num_targets; i++) {
            ASSERT_NE(inj->targets[i], nullptr)
                << name << ": target " << i << " is NULL";
            EXPECT_GT(strlen(inj->targets[i]), 0)
                << name << ": target " << i << " is empty";
        }
    }
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST(InjectorRegistryTest, DefaultConfig_Reasonable) {
    const char* names[] = {
        "getdents_delay", "rename_window", "transaction_abort",
        "vfs_delay", "btrfs_specific", "multi_hook"
    };
    
    for (const char* name : names) {
        const injector_descriptor_t* inj = get_injector(name);
        ASSERT_NE(inj, nullptr);
        
        // Probability should be 0-100%
        EXPECT_GE(inj->config.default_probability_pct, 0)
            << name << ": negative probability";
        EXPECT_LE(inj->config.default_probability_pct, 100)
            << name << ": probability > 100%";
        
        // Delay should be reasonable (0-1000μs)
        EXPECT_GE(inj->config.default_delay_us, 0)
            << name << ": negative delay";
        EXPECT_LE(inj->config.default_delay_us, 1000)
            << name << ": delay too large";
        
        // Min <= default <= max
        EXPECT_LE(inj->config.min_delay_us, inj->config.default_delay_us)
            << name << ": min_delay > default_delay";
        EXPECT_LE(inj->config.default_delay_us, inj->config.max_delay_us)
            << name << ": default_delay > max_delay";
    }
}

// ============================================================================
// Filesystem Compatibility Tests
// ============================================================================

TEST(InjectorRegistryTest, SupportsFilesystem_Generic) {
    const injector_descriptor_t* inj = get_injector("getdents_delay");
    ASSERT_NE(inj, nullptr);
    
    // Generic injectors should support all filesystems
    EXPECT_TRUE(inj->supports_generic);
    EXPECT_TRUE(injector_supports_filesystem(inj, "ext4"));
    EXPECT_TRUE(injector_supports_filesystem(inj, "xfs"));
    EXPECT_TRUE(injector_supports_filesystem(inj, "btrfs"));
    EXPECT_TRUE(injector_supports_filesystem(inj, "f2fs"));
    EXPECT_TRUE(injector_supports_filesystem(inj, "bcachefs"));
}

TEST(InjectorRegistryTest, SupportsFilesystem_BtrfsSpecific) {
    const injector_descriptor_t* inj = get_injector("transaction_abort");
    ASSERT_NE(inj, nullptr);
    
    // Transaction abort is btrfs-specific
    EXPECT_FALSE(inj->supports_generic);
    EXPECT_TRUE(inj->supports_btrfs);
    EXPECT_TRUE(injector_supports_filesystem(inj, "btrfs"));
    
    // Should not support other filesystems
    EXPECT_FALSE(injector_supports_filesystem(inj, "ext4"));
    EXPECT_FALSE(injector_supports_filesystem(inj, "xfs"));
}

TEST(InjectorRegistryTest, SupportsFilesystem_NullArgs) {
    const injector_descriptor_t* inj = get_injector("getdents_delay");
    ASSERT_NE(inj, nullptr);
    
    EXPECT_FALSE(injector_supports_filesystem(nullptr, "ext4"));
    EXPECT_FALSE(injector_supports_filesystem(inj, nullptr));
    EXPECT_FALSE(injector_supports_filesystem(nullptr, nullptr));
}

TEST(InjectorRegistryTest, SupportsFilesystem_UnknownFS) {
    const injector_descriptor_t* inj = get_injector("getdents_delay");
    ASSERT_NE(inj, nullptr);
    
    // Should handle unknown filesystems gracefully
    bool supports = injector_supports_filesystem(inj, "unknown_fs");
    
    if (inj->supports_generic) {
        EXPECT_TRUE(supports) << "Generic injector should support unknown FS";
    } else {
        EXPECT_FALSE(supports) << "Specific injector should not support unknown FS";
    }
}

// ============================================================================
// Requirements Check Tests
// ============================================================================

TEST(InjectorRegistryTest, CheckRequirements_AllInjectors) {
    const char* names[] = {
        "getdents_delay", "rename_window", "transaction_abort",
        "vfs_delay", "btrfs_specific", "multi_hook"
    };
    
    for (const char* name : names) {
        const injector_descriptor_t* inj = get_injector(name);
        ASSERT_NE(inj, nullptr);
        
        char error_buf[256];
        bool ok = injector_check_requirements(inj, error_buf, sizeof(error_buf));
        
        // For now, should always succeed (we don't check yet)
        // When implemented, this might fail based on kernel version
        EXPECT_TRUE(ok) << name << ": " << error_buf;
    }
}

TEST(InjectorRegistryTest, CheckRequirements_NullInjector) {
    char error_buf[256];
    bool ok = injector_check_requirements(nullptr, error_buf, sizeof(error_buf));
    EXPECT_FALSE(ok) << "Should fail for NULL injector";
}

// ============================================================================
// Effectiveness Rating Tests
// ============================================================================

TEST(InjectorRegistryTest, GetdentsDelay_MarkedIneffective) {
    const injector_descriptor_t* inj = get_injector("getdents_delay");
    ASSERT_NE(inj, nullptr);
    
    // Based on research, getdents_delay is ineffective (value = 4)
    EXPECT_EQ(inj->effectiveness, 4)
        << "getdents_delay should be marked ineffective";
}

TEST(InjectorRegistryTest, RenameWindow_HighEffectiveness) {
    const injector_descriptor_t* inj = get_injector("rename_window");
    ASSERT_NE(inj, nullptr);
    
    // Based on research, rename_window should be highly effective (value = 3)
    EXPECT_EQ(inj->effectiveness, 3)
        << "rename_window should be marked highly effective";
}

TEST(InjectorRegistryTest, TransactionAbort_HighEffectiveness) {
    const injector_descriptor_t* inj = get_injector("transaction_abort");
    ASSERT_NE(inj, nullptr);
    
    // High effectiveness (value = 3)
    EXPECT_EQ(inj->effectiveness, 3)
        << "transaction_abort should be marked highly effective";
}

// ============================================================================
// Specific Injector Tests
// ============================================================================

TEST(InjectorRegistryTest, GetdentsDelay_Description) {
    const injector_descriptor_t* inj = get_injector("getdents_delay");
    ASSERT_NE(inj, nullptr);
    
    // Description should mention it's ineffective/legacy
    const char* desc = inj->description;
    EXPECT_TRUE(strstr(desc, "LEGACY") != nullptr ||
                strstr(desc, "legacy") != nullptr ||
                strstr(desc, "ineffective") != nullptr)
        << "Description should warn about ineffectiveness";
}

TEST(InjectorRegistryTest, TransactionAbort_RequiresErrorInjection) {
    const injector_descriptor_t* inj = get_injector("transaction_abort");
    ASSERT_NE(inj, nullptr);
    
    // Transaction abort needs bpf_override_return
    EXPECT_TRUE(inj->requirements.requires_error_injection)
        << "transaction_abort needs CONFIG_BPF_KPROBE_OVERRIDE";
}

TEST(InjectorRegistryTest, VFSDelay_SupportsAllFilesystems) {
    const injector_descriptor_t* inj = get_injector("vfs_delay");
    ASSERT_NE(inj, nullptr);
    
    // VFS layer should work for all filesystems
    EXPECT_TRUE(inj->supports_generic ||
                (inj->supports_ext4 && inj->supports_xfs &&
                 inj->supports_btrfs && inj->supports_f2fs))
        << "vfs_delay should support all major filesystems";
}

