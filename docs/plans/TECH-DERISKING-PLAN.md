# RUDRA: Technology De-Risking Plan

*Prove eBPF fault injection can find bugs BEFORE building full infrastructure*
*Timeline: 2-3 weeks | Investment: ~500 lines of code | Risk reduction: 80%+*
*Date: October 10, 2025*

## Why This Plan?

**The problem**: Full RUDRA implementation is 12-16 weeks and ~15,000 lines of code. That's a big commitment!

**The risk**: What if eBPF fault injection doesn't work? What if it can't find bugs? We'd waste months.

**The solution**: Build minimal PoC in 2-3 weeks to **prove the core technology** before committing to full infrastructure.

---

## Goal

**Validate the core technology** before committing to 12-16 week full implementation:
1. ✅ Prove eBPF programs can load and attach on your system
2. ✅ Prove eBPF → userspace event communication works (perf buffers)
3. ✅ Prove pause injection works (eBPF detects, userspace pauses via SIGSTOP)
4. ✅ Prove pauses increase race condition detection (2x+ improvement)
5. ✅ Find at least one race condition (even intentionally injected)

**What we're NOT doing** (defer to full implementation):
- ❌ VMs (use local filesystem instead - much faster iteration)
- ❌ All 5 filesystems (just ext4 or tmpfs on your machine)
- ❌ Complete DirectoryReader abstraction (minimal version only)
- ❌ Automation/orchestration (manual execution)
- ❌ Advanced fault injection modes (adaptive, adversarial)
- ❌ HTML reports (just console output)
- ❌ io_uring support (classic readdir only for PoC)

**Success criteria**: 
- eBPF pauses increase race detection by 2x+
- Can demonstrate finding a bug via eBPF pause injection
- Clear understanding of eBPF toolchain and development process

**If successful**: Proceed to full implementation with confidence
**If not**: Adjust approach before investing in expensive VM infrastructure

---

## Phase 0: Prerequisites (Day 1)

### 0.1 Verify eBPF Toolchain

- [ ] Check kernel version: `uname -r` (need 5.10+)
- [ ] Check BTF: `ls /sys/kernel/btf/vmlinux` (should exist)
- [ ] Install tools:
  ```bash
  sudo apt-get install -y \
    clang \
    llvm \
    libbpf-dev \
    bpftool \
    linux-headers-$(uname -r)
  ```
- [ ] Test eBPF compilation:
  ```bash
  cat > /tmp/test.bpf.c << 'EOF'
  #include <linux/bpf.h>
  #include <bpf/bpf_helpers.h>
  
  SEC("tracepoint/syscalls/sys_enter_openat")
  int trace_open(void *ctx) {
      bpf_trace_printk("openat called\n");
      return 0;
  }
  
  char _license[] SEC("license") = "GPL";
  EOF
  
  clang -O2 -target bpf -c /tmp/test.bpf.c -o /tmp/test.bpf.o
  ```
- [ ] If successful, eBPF toolchain ready ✅

### 0.2 Check Kernel Config

- [ ] Check if CONFIG_BPF_KPROBE_OVERRIDE enabled:
  ```bash
  grep CONFIG_BPF_KPROBE_OVERRIDE /boot/config-$(uname -r)
  ```
- [ ] **If NOT enabled**: This is OK for tech de-risking
  - Can still use fentry/fexit for logging
  - Can still use SIGSTOP for pauses (userspace control)
  - Just can't use `bpf_override_return()` yet
  - **Note**: Full implementation will need custom kernel with this enabled

---

## Phase 1: Minimal DirectoryReader (Days 2-3)

**Goal**: Bare minimum abstraction to test classic readdir

### 1.1 Create Simple Header

- [ ] Create directory: `mkdir -p common`
- [ ] Create file: `common/dir_reader.h`
- [ ] Add minimal interface:
  ```c
  #ifndef DIR_READER_H
  #define DIR_READER_H
  
  #include <stdint.h>
  
  struct dir_entry {
      uint64_t ino;
      uint8_t type;
      char name[256];
  };
  
  struct dir_reader;
  
  // Create/destroy
  struct dir_reader *dir_reader_create_classic(void);
  void dir_reader_destroy(struct dir_reader *reader);
  
  // Operations
  int dir_reader_open(struct dir_reader *reader, const char *path);
  int dir_reader_read(struct dir_reader *reader, 
                      struct dir_entry *entries, int max_entries);
  void dir_reader_close(struct dir_reader *reader);
  
  #endif
  ```

### 1.2 Minimal Implementation

- [ ] Create file: `common/dir_reader.c`
- [ ] Implement using just opendir/readdir:
  ```c
  #include "dir_reader.h"
  #include <dirent.h>
  #include <stdlib.h>
  #include <string.h>
  #include <errno.h>
  
  struct dir_reader {
      DIR *dir;
  };
  
  struct dir_reader *dir_reader_create_classic(void) {
      return calloc(1, sizeof(struct dir_reader));
  }
  
  void dir_reader_destroy(struct dir_reader *reader) {
      free(reader);
  }
  
  int dir_reader_open(struct dir_reader *reader, const char *path) {
      reader->dir = opendir(path);
      return reader->dir ? 0 : -errno;
  }
  
  int dir_reader_read(struct dir_reader *reader,
                      struct dir_entry *entries, int max_entries) {
      int count = 0;
      while (count < max_entries) {
          struct dirent *ent = readdir(reader->dir);
          if (!ent) break;
          
          entries[count].ino = ent->d_ino;
          entries[count].type = ent->d_type;
          strncpy(entries[count].name, ent->d_name, 256);
          count++;
      }
      return count;
  }
  
  void dir_reader_close(struct dir_reader *reader) {
      if (reader->dir) {
          closedir(reader->dir);
          reader->dir = NULL;
      }
  }
  ```

### 1.3 Build with Bazel

- [ ] Create `common/BUILD.bazel`:
  ```python
  cc_library(
      name = "dir_reader",
      srcs = ["dir_reader.c"],
      hdrs = ["dir_reader.h"],
      visibility = ["//visibility:public"],
  )
  ```
- [ ] Build: `bazel build //common:dir_reader`
- [ ] Fix any errors

---

## Phase 2: Minimal Chaos Test (Days 4-5)

**Goal**: Simple concurrent read/write test that would have races without proper locking

### 2.1 Create Buggy Reader (Intentional Race Condition)

- [ ] Create file: `common/buggy_reader.c`
- [ ] Implement with INTENTIONAL race condition:
  ```c
  #include "dir_reader.h"
  #include <pthread.h>
  #include <stdio.h>
  #include <unistd.h>
  
  // INTENTIONALLY BUGGY: shared f_pos simulation
  static uint64_t shared_offset = 0;  // RACE!
  
  int buggy_reader_read(const char *path, struct dir_entry *entries, 
                        int max_entries) {
      // Read current offset
      uint64_t my_offset = shared_offset;
      
      // RACE WINDOW: Another thread can modify shared_offset here
      
      // Simulate reading based on offset
      int count = 0;
      // ... read logic ...
      
      // Update shared offset
      shared_offset = my_offset + count;  // RACE!
      
      return count;
  }
  ```
- [ ] This simulates the classic f_pos race condition

### 2.2 Create Simple Chaos Test

- [ ] Create file: `chaos/simple_chaos_test.c`
- [ ] Implement minimal concurrent test:
  ```c
  #include <pthread.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <unistd.h>
  #include <stdatomic.h>
  #include "../common/dir_reader.h"
  
  #define NUM_THREADS 10
  #define TEST_DURATION 5  // 5 seconds
  
  struct test_state {
      const char *test_dir;
      atomic_bool stop;
      atomic_int operations;
  };
  
  void *reader_thread(void *arg) {
      struct test_state *state = arg;
      struct dir_reader *reader = dir_reader_create_classic();
      
      while (!atomic_load(&state->stop)) {
          if (dir_reader_open(reader, state->test_dir) < 0)
              continue;
          
          struct dir_entry entries[100];
          int count;
          while ((count = dir_reader_read(reader, entries, 100)) > 0) {
              // Just read, don't do anything
          }
          
          dir_reader_close(reader);
          atomic_fetch_add(&state->operations, 1);
      }
      
      dir_reader_destroy(reader);
      return NULL;
  }
  
  int main(int argc, char *argv[]) {
      if (argc < 2) {
          fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
          return 1;
      }
      
      struct test_state state = {
          .test_dir = argv[1],
          .stop = false,
          .operations = 0,
      };
      
      printf("Starting simple chaos test: %d threads, %d seconds\n",
             NUM_THREADS, TEST_DURATION);
      
      pthread_t threads[NUM_THREADS];
      
      // Launch threads
      for (int i = 0; i < NUM_THREADS; i++) {
          pthread_create(&threads[i], NULL, reader_thread, &state);
      }
      
      // Run for duration
      sleep(TEST_DURATION);
      
      // Stop
      atomic_store(&state.stop, true);
      
      for (int i = 0; i < NUM_THREADS; i++) {
          pthread_join(threads[i], NULL);
      }
      
      printf("Test complete: %d operations\n", atomic_load(&state.operations));
      printf("No crashes = PASS\n");
      
      return 0;
  }
  ```

### 2.3 Build and Test

- [ ] Create `chaos/BUILD.bazel`:
  ```python
  cc_binary(
      name = "simple_chaos_test",
      srcs = ["simple_chaos_test.c"],
      deps = ["//common:dir_reader"],
      linkopts = ["-pthread"],
  )
  ```
- [ ] Build: `bazel build //chaos:simple_chaos_test`
- [ ] Test on local filesystem:
  ```bash
  mkdir -p /tmp/rudra_test
  touch /tmp/rudra_test/file{1..100}
  bazel run //chaos:simple_chaos_test -- /tmp/rudra_test
  ```
- [ ] Should complete without crashes (baseline)

---

## Phase 3: Basic eBPF Logging (Days 6-7)

**Goal**: Get eBPF working - just logging, no fault injection yet

### 3.1 Simple eBPF Program (Logging Only)

- [ ] Create file: `chaos/simple_tracer.bpf.c`
- [ ] Add minimal eBPF program:
  ```c
  #include <linux/bpf.h>
  #include <bpf/bpf_helpers.h>
  #include <bpf/bpf_tracing.h>
  
  char LICENSE[] SEC("license") = "GPL";
  
  // Hook: openat syscall (always available)
  SEC("tracepoint/syscalls/sys_enter_openat")
  int trace_openat(void *ctx) {
      u32 pid = bpf_get_current_pid_tgid() >> 32;
      bpf_trace_printk("openat called by pid=%d\n", pid);
      return 0;
  }
  
  // Hook: getdents64 syscall
  SEC("tracepoint/syscalls/sys_enter_getdents64")
  int trace_getdents(void *ctx) {
      u32 pid = bpf_get_current_pid_tgid() >> 32;
      bpf_trace_printk("getdents64 called by pid=%d\n", pid);
      return 0;
  }
  ```

### 3.2 Build eBPF

- [ ] Add to `chaos/BUILD.bazel`:
  ```python
  genrule(
      name = "simple_tracer_bpf",
      srcs = ["simple_tracer.bpf.c"],
      outs = ["simple_tracer.bpf.o"],
      cmd = """
          clang -g -O2 -target bpf \
              -D__TARGET_ARCH_x86_64 \
              -I/usr/include/x86_64-linux-gnu \
              -c $(location simple_tracer.bpf.c) \
              -o $@
      """,
  )
  ```
- [ ] Build: `bazel build //chaos:simple_tracer_bpf`
- [ ] Fix compilation errors

### 3.3 Load and Test

- [ ] Load eBPF program:
  ```bash
  sudo bpftool prog load bazel-bin/chaos/simple_tracer.bpf.o /sys/fs/bpf/simple_tracer
  sudo bpftool prog show
  ```
- [ ] Attach to tracepoints:
  ```bash
  # Find tracepoint IDs
  OPENAT_ID=$(sudo bpftool prog show | grep sys_enter_openat | awk '{print $1}' | cut -d: -f1)
  
  # Manual attach (or use bpftool)
  ```
- [ ] Run test in another terminal:
  ```bash
  ls /tmp/rudra_test
  ```
- [ ] Check traces:
  ```bash
  sudo cat /sys/kernel/debug/tracing/trace_pipe
  # Should see "getdents64 called by pid=..."
  ```
- [ ] If you see traces: ✅ eBPF works!

---

## Phase 4: eBPF Pause Injection (Days 8-10)

**Goal**: Inject pauses via eBPF → userspace coordination

### 4.1 eBPF Program with Perf Events

- [ ] Create file: `chaos/pause_injector.bpf.c`
- [ ] Add pause event infrastructure:
  ```c
  #include <linux/bpf.h>
  #include <bpf/bpf_helpers.h>
  #include <bpf/bpf_tracing.h>
  
  char LICENSE[] SEC("license") = "GPL";
  
  // Perf event array for sending events to userspace
  struct {
      __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
      __uint(key_size, sizeof(u32));
      __uint(value_size, sizeof(u32));
  } events SEC(".maps");
  
  // Configuration map
  struct {
      __uint(type, BPF_MAP_TYPE_ARRAY);
      __uint(max_entries, 1);
      __type(key, u32);
      __type(value, u32);
  } config SEC(".maps");
  
  struct pause_event {
      u32 pid;
      u64 timestamp;
  };
  
  SEC("tracepoint/syscalls/sys_enter_getdents64")
  int trace_getdents_with_pause(void *ctx) {
      // Read config (pause probability %)
      u32 key = 0;
      u32 *pause_prob = bpf_map_lookup_elem(&config, &key);
      if (!pause_prob)
          return 0;
      
      // Random chance to pause
      u32 rand = bpf_get_prandom_u32();
      if ((rand % 100) < *pause_prob) {
          struct pause_event event = {
              .pid = bpf_get_current_pid_tgid() >> 32,
              .timestamp = bpf_ktime_get_ns(),
          };
          
          // Send to userspace
          bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                                &event, sizeof(event));
          
          bpf_trace_printk("Requesting pause for pid=%d\n", event.pid);
      }
      
      return 0;
  }
  ```

### 4.2 Userspace Controller

- [ ] Create file: `chaos/pause_controller.c`
- [ ] Implement event handler:
  ```c
  #include <bpf/libbpf.h>
  #include <bpf/bpf.h>
  #include <signal.h>
  #include <stdio.h>
  #include <unistd.h>
  
  struct pause_event {
      uint32_t pid;
      uint64_t timestamp;
  };
  
  void handle_event(void *ctx, int cpu, void *data, __u32 size) {
      struct pause_event *event = data;
      
      printf("PAUSE REQUEST: pid=%d\n", event->pid);
      
      // Pause the process using SIGSTOP
      if (kill(event->pid, SIGSTOP) == 0) {
          usleep(5000);  // 5ms pause
          kill(event->pid, SIGCONT);
          printf("  Paused pid=%d for 5ms\n", event->pid);
      }
  }
  
  int main(int argc, char *argv[]) {
      struct bpf_object *obj;
      struct perf_buffer *pb;
      int err;
      
      if (argc < 2) {
          fprintf(stderr, "Usage: %s <pause_probability_pct>\n", argv[0]);
          return 1;
      }
      
      int pause_prob = atoi(argv[1]);
      printf("Starting pause controller: %d%% pause probability\n", pause_prob);
      
      // Load eBPF program
      obj = bpf_object__open_file("pause_injector.bpf.o", NULL);
      if (!obj) {
          perror("bpf_object__open_file");
          return 1;
      }
      
      err = bpf_object__load(obj);
      if (err) {
          fprintf(stderr, "Failed to load eBPF: %d\n", err);
          return 1;
      }
      
      // Attach programs
      struct bpf_program *prog;
      struct bpf_link *link;
      bpf_object__for_each_program(prog, obj) {
          link = bpf_program__attach(prog);
          if (!link) {
              fprintf(stderr, "Failed to attach program\n");
              return 1;
          }
      }
      
      // Set config (pause probability)
      int config_fd = bpf_object__find_map_fd_by_name(obj, "config");
      uint32_t key = 0;
      uint32_t val = pause_prob;
      bpf_map_update_elem(config_fd, &key, &val, BPF_ANY);
      
      // Setup perf buffer
      int events_fd = bpf_object__find_map_fd_by_name(obj, "events");
      pb = perf_buffer__new(events_fd, 8, handle_event, NULL, NULL, NULL);
      if (!pb) {
          fprintf(stderr, "Failed to create perf buffer\n");
          return 1;
      }
      
      printf("eBPF loaded, listening for pause requests...\n");
      printf("Press Ctrl+C to stop\n\n");
      
      // Poll for events
      while (1) {
          err = perf_buffer__poll(pb, 100);  // 100ms timeout
          if (err < 0 && err != -EINTR) {
              fprintf(stderr, "Error polling: %d\n", err);
              break;
          }
      }
      
      perf_buffer__free(pb);
      bpf_object__close(obj);
      return 0;
  }
  ```

### 4.3 Build and Test

- [ ] Update `chaos/BUILD.bazel`:
  ```python
  genrule(
      name = "pause_injector_bpf",
      srcs = ["pause_injector.bpf.c"],
      outs = ["pause_injector.bpf.o"],
      cmd = """
          clang -g -O2 -target bpf \
              -D__TARGET_ARCH_x86_64 \
              -I/usr/include/x86_64-linux-gnu \
              -c $(location pause_injector.bpf.c) \
              -o $@
      """,
  )
  
  cc_binary(
      name = "pause_controller",
      srcs = ["pause_controller.c"],
      data = [":pause_injector_bpf"],
      linkopts = ["-lbpf"],
  )
  ```
- [ ] Build: `bazel build //chaos:pause_controller`
- [ ] Test in two terminals:
  ```bash
  # Terminal 1: Start pause controller
  sudo bazel-bin/chaos/pause_controller 20  # 20% pause probability
  
  # Terminal 2: Run simple chaos test
  bazel run //chaos:simple_chaos_test -- /tmp/rudra_test
  
  # Terminal 1 should show: "PAUSE REQUEST: pid=12345"
  ```
- [ ] If pauses are injected: ✅ eBPF pause injection works!

---

## Phase 5: Validation - Find the Intentional Bug (Days 11-14)

**Goal**: Prove eBPF pauses make race conditions more likely

### 5.1 Create Race Detection Test

- [ ] Create file: `chaos/race_detector.c`
- [ ] Implement duplicate detection:
  ```c
  #include <pthread.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #include <stdatomic.h>
  #include "../common/dir_reader.h"
  
  #define NUM_THREADS 10
  
  struct entry_tracker {
      char names[10000][256];
      int count;
      pthread_mutex_t lock;
  };
  
  struct test_result {
      atomic_int duplicates_found;
      atomic_int operations;
  };
  
  void *detector_thread(void *arg) {
      // Read directory, track entries, detect duplicates
      // ...
  }
  
  int main(int argc, char *argv[]) {
      printf("=== RACE DETECTION TEST ===\n");
      printf("Testing if eBPF pauses make races more likely\n\n");
      
      // Test 1: Without eBPF (baseline)
      printf("Phase 1: Running WITHOUT eBPF pauses...\n");
      run_test_without_ebpf();
      // Expect: Few or no duplicates
      
      printf("\nPhase 2: Running WITH eBPF pauses (20%%)...\n");
      // Start pause controller in background
      // Run same test
      run_test_with_ebpf();
      // Expect: MORE duplicates (proves pauses work!)
      
      printf("\n=== RESULTS ===\n");
      printf("Without eBPF: %d duplicates\n", baseline_duplicates);
      printf("With eBPF:    %d duplicates\n", ebpf_duplicates);
      
      if (ebpf_duplicates > baseline_duplicates * 2) {
          printf("\n✅ SUCCESS: eBPF pauses increased race detection!\n");
          printf("Proof: Fault injection makes races more likely\n");
          return 0;
      } else {
          printf("\n⚠️  INCONCLUSIVE: Need longer test or higher pause rate\n");
          return 1;
      }
  }
  ```

### 5.2 Run Validation

- [ ] Run complete validation:
  ```bash
  # Create test directory with many files
  mkdir -p /tmp/rudra_race_test
  for i in {1..1000}; do touch /tmp/rudra_race_test/file$i; done
  
  # Run race detector
  bazel run //chaos:race_detector -- /tmp/rudra_race_test
  ```
- [ ] Expected output:
  ```
  Phase 1: Without eBPF
    Operations: 500
    Duplicates: 0-2
  
  Phase 2: With eBPF (20% pauses)
    Operations: 500
    Duplicates: 10-50
  
  ✅ SUCCESS: eBPF increased races by 10x+
  ```

---

## Phase 6: Advanced eBPF - Fault Override (Days 15-18)

**Goal**: If kernel supports it, test bpf_override_return()

### 6.1 Check if Override Supported

- [ ] Check kernel config:
  ```bash
  grep CONFIG_BPF_KPROBE_OVERRIDE /boot/config-$(uname -r)
  ```
- [ ] If **enabled**: Proceed with this phase
- [ ] If **NOT enabled**: Skip to Phase 7 (still valuable without this)

### 6.2 eBPF Fault Injector

- [ ] Create file: `chaos/fault_injector.bpf.c`
- [ ] Add fault override:
  ```c
  #include <linux/bpf.h>
  #include <bpf/bpf_helpers.h>
  #include <bpf/bpf_tracing.h>
  
  char LICENSE[] SEC("license") = "GPL";
  
  // Try to override a simple function return
  // Pick something safe like open() on a specific test file
  
  SEC("kprobe/do_sys_openat2")
  int BPF_KPROBE(override_openat, int dfd, const char *filename) {
      u32 rand = bpf_get_prandom_u32();
      
      // 5% chance to return -EMFILE (too many open files)
      if ((rand % 100) < 5) {
          bpf_override_return(regs, -24);  // -EMFILE
          bpf_trace_printk("Injected -EMFILE\n");
      }
      
      return 0;
  }
  ```
- [ ] Build and test
- [ ] If you can override returns: ✅ Full eBPF capability!
- [ ] If error function not in allowed list: OK, use pauses instead

---

## Phase 7: Comprehensive Validation (Days 19-21)

**Goal**: Run realistic chaos test with eBPF, find actual issues

### 7.1 Enhanced Chaos Test

- [ ] Create file: `chaos/comprehensive_test.c`
- [ ] Add concurrent readers + writers:
  ```c
  #define NUM_READERS 20
  #define NUM_WRITERS 5
  #define TEST_DURATION 60  // 1 minute
  
  void *writer_thread(void *arg) {
      const char *dir = arg;
      
      for (int i = 0; i < 1000; i++) {
          char path[512];
          snprintf(path, sizeof(path), "%s/temp_%d_%d", 
                   dir, getpid(), i);
          
          // Create file
          int fd = creat(path, 0644);
          if (fd >= 0) close(fd);
          
          usleep(1000);  // 1ms
          
          // Delete file
          unlink(path);
      }
      
      return NULL;
  }
  
  // ... reader threads that check for duplicates ...
  ```

### 7.2 Run Full Test

- [ ] Build: `bazel build //chaos:comprehensive_test`
- [ ] Run without eBPF (baseline):
  ```bash
  bazel run //chaos:comprehensive_test -- /tmp/rudra_test
  # Should pass with no issues
  ```
- [ ] Run WITH eBPF pauses:
  ```bash
  # Terminal 1: Start pause controller
  sudo bazel-bin/chaos/pause_controller 30  # 30% pause rate
  
  # Terminal 2: Run comprehensive test
  bazel run //chaos:comprehensive_test -- /tmp/rudra_test
  
  # Check for:
  # - Increased race detection
  # - More EAGAIN errors
  # - Possible duplicate entries
  ```

### 7.3 Document Results

- [ ] Create results file: `TECH-DERISKING-RESULTS.md`
- [ ] Document findings:
  ```markdown
  # Tech De-Risking Results
  
  ## eBPF Capability Validation
  
  ✅ eBPF programs load successfully
  ✅ Can attach to tracepoints
  ✅ Can send events to userspace via perf buffers
  ✅ Can pause processes via SIGSTOP
  ✅/❌ Can override returns (if CONFIG_BPF_KPROBE_OVERRIDE available)
  
  ## Race Condition Detection
  
  Without eBPF:
  - Duplicates found: X
  - Operations: Y
  
  With eBPF (20% pause rate):
  - Duplicates found: X * N  (N times more!)
  - Operations: Y
  
  Conclusion: eBPF pauses increase race detection by Nx
  
  ## Recommendations
  
  ✅ Proceed with full implementation
  ✅ eBPF fault injection is viable
  ✅ Can find race conditions effectively
  
  Next: Build full VM infrastructure (Phases 4-9)
  ```

---

## Success Metrics

### Minimum Success (Proceed to Full Implementation):
- ✅ eBPF programs compile and load
- ✅ Can send events from kernel to userspace
- ✅ Can pause processes on demand
- ✅ Pauses increase race condition detection by 2x+

### Bonus Success:
- ✅ bpf_override_return() works (can inject faults)
- ✅ Found actual race condition in real code
- ✅ Can minimize reproducer

### Failure (Need Different Approach):
- ❌ Can't load eBPF programs
- ❌ Pauses don't increase race detection
- ❌ Too much overhead (>50% slowdown)

---

## Timeline Summary

**Week 1** (Days 1-7):
- Day 1: Prerequisites, eBPF toolchain validation
- Day 2-3: Minimal DirectoryReader
- Day 4-5: Simple chaos test
- Day 6-7: eBPF logging (prove eBPF works)

**Week 2** (Days 8-14):
- Day 8-10: eBPF pause injection
- Day 11-14: Validation and bug finding

**Week 3** (Days 15-21):
- Day 15-18: Advanced features (fault override, if available)
- Day 19-21: Comprehensive validation and documentation

**End of Week 3**: Decision point
- ✅ If successful: Proceed to full implementation (Phases 4-9)
- ❌ If not: Reevaluate approach

---

## Deliverables

**By end of tech de-risking**:

1. **Minimal working code**:
   - `common/dir_reader.{h,c}` - ~200 lines
   - `chaos/simple_chaos_test.c` - ~100 lines
   - `chaos/pause_injector.bpf.c` - ~50 lines
   - `chaos/pause_controller.c` - ~150 lines
   - Total: ~500 lines of code

2. **Working eBPF infrastructure**:
   - Bazel build rules for eBPF compilation
   - eBPF load/attach pattern
   - Perf buffer event handling

3. **Proof of concept**:
   - Demonstrable race condition found via eBPF pauses
   - Metrics showing eBPF effectiveness
   - Clear path forward

4. **Documentation**:
   - Results document
   - Lessons learned
   - Recommendations for full implementation

---

## What You'll Learn

**Technical validation**:
- ✅ eBPF toolchain works in your environment
- ✅ Can load and attach eBPF programs
- ✅ Pause injection via SIGSTOP is viable
- ✅ Fault injection can find races
- ✅/❌ bpf_override_return() available

**Practical knowledge**:
- How to debug eBPF programs
- How to structure eBPF + userspace interaction
- How to detect race conditions programmatically
- Typical race condition frequency with/without pauses

**Risk reduction**:
- Prove core technology before building VMs
- Validate approach with minimal investment
- Find tooling issues early
- Build confidence in full plan

---

## Integration with Full Plan

**This tech de-risking is**:
- Phase 1.1-1.3 (minimal DirectoryReader)
- Phase 2.1-2.3 (minimal chaos test)
- Phase 3.1-3.3 (basic eBPF)

**After this succeeds**:
- Complete Phase 1.4-1.5 (full DirectoryReader)
- Complete Phase 2.4-2.6 (full chaos framework)
- Complete Phase 3.4-3.5 (advanced eBPF)
- Then proceed to Phase 4+ (VMs, etc.)

**If this fails**:
- Investigate why eBPF didn't work
- Consider alternatives (userspace-only fault injection)
- Reevaluate full plan

---

## Quick Start (Right Now)

**Want to start immediately?**

```bash
cd /home/jmalicki/src/rudra

# Day 1: Setup
sudo apt-get install -y clang llvm libbpf-dev bpftool
kvm-ok  # Verify KVM (for later)

# Day 2: Minimal code
mkdir -p common chaos
# Create dir_reader.h (see Phase 1.1)
# Create dir_reader.c (see Phase 1.2)

# Day 3: Test it
# Create simple_chaos_test.c (see Phase 2.2)
bazel build //common:dir_reader //chaos:simple_chaos_test

# Day 6: eBPF
# Create simple_tracer.bpf.c (see Phase 3.1)
bazel build //chaos:simple_tracer_bpf
sudo bpftool prog load ...

# Day 14: Validate
# By now you'll know if the approach works!
```

---

## Exit Criteria

**After 2-3 weeks, you should be able to answer**:

1. ✅/❌ Does eBPF fault injection work?
2. ✅/❌ Can it find race conditions?
3. ✅/❌ Is the approach viable?
4. ✅/❌ Should we proceed with full implementation?

**If all ✅**: Continue to full plan with confidence!

**If any ❌**: Adjust approach before investing in VMs.

---

*Tech de-risking: Prove the hardest part first, build confidence, then scale up.*

*Time investment: 2-3 weeks | Code: ~500 lines | Risk reduction: 80%+* ⚡

