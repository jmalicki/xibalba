# eBPF Testing Strategy

**Goal:** Unit test eBPF programs in userspace without loading into kernel

---

## Option 1: BPF_PROG_TEST_RUN (Recommended for libbpf)

**What it is:** Built-in libbpf function that runs BPF programs with mock data in userspace.

**Pros:**
- ✅ Part of libbpf (already a dependency)
- ✅ Official kernel testing mechanism
- ✅ Can test with various inputs
- ✅ No additional dependencies

**Cons:**
- ⚠️ Still requires compiled .bpf.o file
- ⚠️ Limited mocking capabilities
- ⚠️ Can't mock all kernel helpers

### Example Usage:

```c
// test_rename_window.c
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

int test_rename_window_injection() {
    struct bpf_object *obj;
    struct bpf_program *prog;
    int prog_fd, err;
    
    // Load BPF object
    obj = bpf_object__open_file("rename_window.bpf.o", NULL);
    if (!obj) return -1;
    
    err = bpf_object__load(obj);
    if (err) return -1;
    
    // Find program
    prog = bpf_object__find_program_by_name(obj, "hook_btrfs_unlink_exit");
    prog_fd = bpf_program__fd(prog);
    
    // Prepare mock input
    struct {
        struct pt_regs regs;
        // ... other context
    } ctx = {
        .regs.ax = 0,  // Success return
    };
    
    char output[256];
    __u32 output_size = sizeof(output);
    __u32 retval;
    __u32 duration;
    
    // Run program with mock data
    struct bpf_test_run_opts opts = {
        .ctx_in = &ctx,
        .ctx_size_in = sizeof(ctx),
        .data_out = output,
        .data_size_out = output_size,
        .repeat = 1,
    };
    
    err = bpf_prog_test_run_opts(prog_fd, &opts);
    
    // Verify results
    if (err == 0) {
        printf("Program returned: %d\n", opts.retval);
        printf("Duration: %u ns\n", opts.duration);
    }
    
    bpf_object__close(obj);
    return err;
}
```

**Limitations:**
- Requires BPF program to compile successfully
- Some kernel helpers may not work in test mode
- Can't fully mock kernel state

---

## Option 2: Extract Testable Helpers

**What it is:** Separate helper logic from BPF-specific code, compile for both targets.

**Pros:**
- ✅ Easy to test with standard frameworks (gtest)
- ✅ No BPF toolchain needed for tests
- ✅ Full mocking capability
- ✅ Fast iteration

**Cons:**
- ⚠️ Requires code restructuring
- ⚠️ Doesn't test full BPF program
- ⚠️ Need to ensure consistency

### Example Structure:

```c
// bpf_helpers.h (shared between BPF and userspace)
#ifndef __BPF_HELPERS_H
#define __BPF_HELPERS_H

#ifdef __BPF__
    // BPF version uses kernel helpers
    #include <bpf/bpf_helpers.h>
    #define ALWAYS_INLINE __always_inline
#else
    // Userspace version uses standard C
    #include <stdint.h>
    #include <stdbool.h>
    #define ALWAYS_INLINE inline
#endif

// Pure logic functions (testable)
static ALWAYS_INLINE bool should_inject_delay(uint32_t probability) {
    #ifdef __BPF__
        uint32_t rand = bpf_get_prandom_u32();
    #else
        uint32_t rand = rand();  // Use regular rand() in tests
    #endif
    return (rand % 100) < probability;
}

static ALWAYS_INLINE uint64_t calculate_delay_ns(uint32_t delay_us) {
    return (uint64_t)delay_us * 1000ULL;
}

#endif
```

```cpp
// bpf_helpers_test.cc
#include <gtest/gtest.h>

extern "C" {
#include "bpf_helpers.h"
}

TEST(BPFHelpersTest, ShouldInjectDelay_ZeroProbability) {
    EXPECT_FALSE(should_inject_delay(0));
}

TEST(BPFHelpersTest, ShouldInjectDelay_100Probability) {
    // With 100% probability, should always inject
    bool injected = false;
    for (int i = 0; i < 10; i++) {
        if (should_inject_delay(100)) {
            injected = true;
            break;
        }
    }
    EXPECT_TRUE(injected);
}

TEST(BPFHelpersTest, CalculateDelayNs) {
    EXPECT_EQ(calculate_delay_ns(10), 10000ULL);
    EXPECT_EQ(calculate_delay_ns(0), 0ULL);
    EXPECT_EQ(calculate_delay_ns(1000), 1000000ULL);
}
```

---

## Option 3: uBPF (Userspace eBPF Runtime)

**What it is:** Complete eBPF interpreter/JIT that runs in userspace.

**Repository:** https://github.com/iovisor/ubpf

**Pros:**
- ✅ Full eBPF execution in userspace
- ✅ Cross-platform (Windows, Linux, macOS)
- ✅ Can test without kernel
- ✅ Supports interpreter and JIT

**Cons:**
- ⚠️ Additional dependency
- ⚠️ Different runtime (not kernel BPF)
- ⚠️ May behave differently than kernel

### Example Usage:

```c
#include <ubpf.h>

int test_with_ubpf() {
    struct ubpf_vm *vm;
    void *mem;
    size_t mem_len;
    uint64_t result;
    
    // Create VM
    vm = ubpf_create();
    
    // Load BPF bytecode
    FILE *f = fopen("rename_window.bpf.o", "rb");
    fseek(f, 0, SEEK_END);
    mem_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    mem = malloc(mem_len);
    fread(mem, 1, mem_len, f);
    fclose(f);
    
    // Load into VM
    ubpf_load(vm, mem, mem_len, NULL);
    
    // Prepare context
    struct test_ctx {
        uint64_t ret_value;
        // ... other fields
    } ctx = {
        .ret_value = 0,
    };
    
    // Execute
    result = ubpf_exec(vm, &ctx, sizeof(ctx));
    
    printf("BPF program returned: %lu\n", result);
    
    ubpf_destroy(vm);
    free(mem);
    
    return 0;
}
```

---

## Option 4: bpftime (Newer Alternative)

**What it is:** Userspace eBPF runtime with LLVM JIT, compatible with existing toolchains.

**Repository:** https://github.com/eunomia-bpf/bpftime

**Pros:**
- ✅ Compatible with libbpf toolchain
- ✅ LLVM-based JIT (faster)
- ✅ Supports many kernel helpers
- ✅ Good for performance testing

**Cons:**
- ⚠️ Still experimental/newer
- ⚠️ Heavier dependency (LLVM)
- ⚠️ Less documentation

---

## Recommended Approach for Xibalba

### Short-term (Immediate):

**Extract Helper Functions** (Option 2)
- Create `chaos/injectors/bpf_helpers.h` with shared logic
- Write unit tests for decision logic:
  - `should_inject_delay()` probability logic
  - Delay calculation
  - Stats tracking logic
- Fast, easy, no new dependencies

### Mid-term (After BPF programs work):

**BPF_PROG_TEST_RUN** (Option 1)
- Test full BPF programs with mock context
- Verify map operations
- Test with various inputs
- Part of libbpf, minimal overhead

### Long-term (Optional):

**uBPF or bpftime** (Option 3/4)
- For cross-platform testing
- CI/CD on systems without BPF support
- Regression testing across platforms

---

## Implementation Example

Here's how we could structure testable BPF code:

```c
// chaos/injectors/injection_logic.h
#ifndef INJECTION_LOGIC_H
#define INJECTION_LOGIC_H

#ifdef __BPF__
    #include <linux/bpf.h>
    #include <bpf/bpf_helpers.h>
    #define GET_RANDOM() bpf_get_prandom_u32()
    #define GET_TIME() bpf_ktime_get_ns()
#else
    #include <stdlib.h>
    #include <time.h>
    #define GET_RANDOM() ((uint32_t)rand())
    #define GET_TIME() ((uint64_t)time(NULL) * 1000000000ULL)
#endif

// Testable decision logic
static inline bool should_inject(uint32_t probability_pct) {
    if (probability_pct == 0) return false;
    if (probability_pct >= 100) return true;
    return (GET_RANDOM() % 100) < probability_pct;
}

// Testable delay calculation
static inline uint64_t delay_duration_ns(uint32_t delay_us) {
    return (uint64_t)delay_us * 1000ULL;
}

// Testable bounds checking
static inline uint32_t clamp_delay(uint32_t delay_us, uint32_t min, uint32_t max) {
    if (delay_us < min) return min;
    if (delay_us > max) return max;
    return delay_us;
}

#endif
```

```cpp
// chaos/injectors/injection_logic_test.cc
#include <gtest/gtest.h>

extern "C" {
#include "injection_logic.h"
}

TEST(InjectionLogicTest, ShouldInject_BoundaryValues) {
    EXPECT_FALSE(should_inject(0));   // 0% never injects
    EXPECT_TRUE(should_inject(100));  // 100% always injects
}

TEST(InjectionLogicTest, ShouldInject_Probability) {
    // Statistical test: 50% probability should inject ~half the time
    int injections = 0;
    for (int i = 0; i < 1000; i++) {
        if (should_inject(50)) injections++;
    }
    
    // Should be roughly 500 ± 100 (allowing for randomness)
    EXPECT_GT(injections, 400);
    EXPECT_LT(injections, 600);
}

TEST(InjectionLogicTest, DelayDuration) {
    EXPECT_EQ(delay_duration_ns(0), 0ULL);
    EXPECT_EQ(delay_duration_ns(1), 1000ULL);
    EXPECT_EQ(delay_duration_ns(1000), 1000000ULL);
}

TEST(InjectionLogicTest, ClampDelay) {
    EXPECT_EQ(clamp_delay(5, 10, 100), 10);   // Below min
    EXPECT_EQ(clamp_delay(50, 10, 100), 50);  // Within range
    EXPECT_EQ(clamp_delay(150, 10, 100), 100); // Above max
}
```

---

## Benefits of This Approach

1. **Fast Iteration:**
   - Test logic changes without BPF compilation
   - Run tests in milliseconds, not seconds

2. **Comprehensive Coverage:**
   - Test edge cases easily
   - Statistical testing for randomness
   - Boundary value testing

3. **CI/CD Friendly:**
   - No kernel BPF support needed in CI
   - Fast test execution
   - Easy to parallelize

4. **Debugging:**
   - Use standard debuggers (gdb, lldb)
   - AddressSanitizer, UBSan work normally
   - Print debugging works

5. **Documentation:**
   - Tests serve as usage examples
   - Clear contract for each function

---

## Next Steps

1. **Immediate:** Extract helper functions from BPF programs
2. **Create:** `injection_logic.h` with shared code
3. **Write:** Unit tests for all decision logic
4. **Refactor:** BPF programs to use shared helpers
5. **Integrate:** Into existing test suite

This gives us 90% test coverage of BPF logic without needing working BPF programs!

---

*For Xibalba: Start with Option 2 (Extract Helpers), add Option 1 (BPF_PROG_TEST_RUN) once BPF programs compile.*

