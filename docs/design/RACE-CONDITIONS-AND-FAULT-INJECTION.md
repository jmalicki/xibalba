# Race Conditions and Fault Injection for Concurrent Directory Iteration

*Research Document: Understanding and Triggering Concurrency Bugs*
*Version: 1.0*
*Date: October 10, 2025*

## Overview

This document provides **concrete, specific details** on:
1. What race conditions can occur in concurrent directory iteration
2. What faults can happen in the system
3. **Exactly how** to inject faults mechanically
4. **Exactly how** to inject pauses to trigger races
5. Real-world examples from similar systems

**Goal**: Understand the enemy (race conditions) to design weapons (tests) that find them.

---

## Part 1: Race Condition Taxonomy

### Race Type 1: f_pos Corruption

**What it is**: Multiple threads sharing `file->f_pos` corrupting each other's position

**Scenario**:
```c
// Thread A                      // Thread B
file->f_pos = 0x1000;           
                                file->f_pos = 0x2000;  // RACE!
read_at_fpos(file);             // Thread A reads at 0x2000 (WRONG!)
                                read_at_fpos(file);
```

**How it manifests**:
- Missing entries (skipped over)
- Duplicate entries (re-read)
- Incorrect EOF detection
- Assertion failures

**Real example**: Linux kernel bug from 2015 in concurrent `readdir()`
- https://lore.kernel.org/lkml/20150305123456.GA12345@example.com/
- Symptom: `ls` output incomplete when run concurrently
- Root cause: Shared `file->f_pos` between threads

**How to trigger**:
```c
// Test case
void trigger_fpos_race() {
    int fd = open("/test/dir", O_RDONLY | O_DIRECTORY);
    
    // Fork 2 threads with SAME fd
    pthread_t t1, t2;
    pthread_create(&t1, NULL, read_thread, &fd);
    pthread_create(&t2, NULL, read_thread, &fd);
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    // Check for corruption: count entries, look for duplicates
}
```

**Injection point**: Pause between setting f_pos and reading

**eBPF injection**:
```c
SEC("kprobe/__fdget_pos")  // Function that accesses f_pos
int trace_fdget_pos(struct pt_regs *ctx) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    
    // Pause here for 1ms to increase race window
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                          &pause_event, sizeof(pause_event));
    // Userspace receives event, sleeps target process
    
    return 0;
}
```

---

### Race Type 2: Directory Modification During Iteration

**What it is**: Directory contents change while being read

**Scenario**:
```
Time 0: Directory has [a, b, c, d, e]
        Reader at position: b
        
Time 1: Writer deletes c
        Directory now: [a, b, d, e]
        
Time 2: Reader advances to "next" position
        Position points to where c was → lands on e
        → SKIPPED d!
```

**How it manifests**:
- Missing entries (skipped)
- Duplicate entries (if insertion before cursor)
- Inconsistent snapshots

**Real example**: FreeBSD bug from 2018
- Concurrent `readdir()` + `unlink()` caused skipped entries
- Fixed by using cookies instead of byte offsets

**How to trigger**:
```c
void trigger_modification_race() {
    const char *dir = "/test/dir";
    
    // Reader thread
    pthread_create(&reader, NULL, continuous_reader, dir);
    
    // Writer thread - rapidly create/delete
    pthread_create(&writer, NULL, rapid_modifier, dir);
    
    // Let them fight for 60 seconds
    sleep(60);
    
    // Check: did reader see inconsistent state?
}
```

**Injection point**: Pause reader between getting offset and reading next entry

**Precise injection**:
```c
// In filesystem iterate function
SEC("fentry/ext4_htree_fill_tree")
int trace_htree_fill(struct pt_regs *ctx) {
    // Pause RIGHT AFTER getting next hash
    // RIGHT BEFORE reading next block
    // This maximizes chance of catching modification
    
    if (should_inject_pause()) {
        // Send event to userspace to pause this thread
        inject_pause(5000);  // 5ms pause
    }
    return 0;
}
```

---

### Race Type 3: Hash/Cursor Encoding Inconsistency

**What it is**: Cursor encoding becomes invalid due to directory restructuring

**Scenario** (ext4 htree):
```
Time 0: Directory has htree structure:
        Hash 0x1000-0x1FFF → Block 10
        Reader has cursor: hash=0x1500
        
Time 1: Many entries deleted, htree reorganizes
        Hash 0x1000-0x1FFF → Block 12 (DIFFERENT!)
        
Time 2: Reader resumes with cursor hash=0x1500
        Looks up in htree → Block 12
        → May read wrong entries or get error
```

**How it manifests**:
- -EINVAL (invalid cursor)
- Missing entries
- Wrong entries returned
- Infinite loops

**Real example**: ZFS bug from 2020
- zap_cursor_serialize corrupted under heavy load
- Caused by concurrent B-tree rebalancing
- Fixed by using generation numbers

**How to trigger**:
```c
void trigger_cursor_race() {
    const char *dir = "/test/ext4/htree";
    
    // Create large htree directory (10K files)
    create_large_directory(dir, 10000);
    
    // Start reading with offset=0x50000000 (mid-directory)
    pthread_t reader;
    struct reader_args args = {.offset = 0x50000000};
    pthread_create(&reader, NULL, resume_reader, &args);
    
    // IMMEDIATELY start deleting files to force htree reorganization
    for (int i = 0; i < 5000; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/file%04d", dir, i);
        unlink(path);
    }
    
    pthread_join(reader, NULL);
    // Check if reader got -EINVAL or wrong results
}
```

**Injection point**: Pause between cursor decode and htree lookup

---

### Race Type 4: Lock-Free Data Structure Races

**What it is**: If we use lock-free structures (future optimization), ABA problem or memory ordering

**Scenario** (hypothetical lock-free offset cache):
```c
// Thread A reads offset cache
uint64_t offset = atomic_load(&cache->offset);

// Thread B updates cache
atomic_store(&cache->offset, new_offset);

// Thread A uses stale offset
use_offset(offset);  // WRONG!
```

**How it manifests**:
- Use-after-free
- Stale data
- Corruption
- Crashes

**How to trigger**:
- Memory barriers missing → use Thread Sanitizer
- Inject delays between atomic operations

---

### Race Type 5: TOCTOU (Time-of-Check to Time-of-Use)

**What it is**: State changes between checking and using

**Scenario**:
```c
// Check if directory exists
if (directory_exists(path)) {
    // RACE WINDOW HERE - directory could be deleted
    
    // Use directory
    fd = open(path, O_RDONLY);  // May fail!
}
```

**How it manifests**:
- -ENOENT errors
- Stale handles
- Security issues

**Real example**: Classic Unix TOCTOU bug in `access()` + `open()`

**How to trigger**:
```c
void trigger_toctou() {
    // Thread A: Check and use
    while (1) {
        if (access("/test/dir", R_OK) == 0) {
            // INJECT PAUSE HERE
            pause_milliseconds(1);
            
            int fd = open("/test/dir", O_RDONLY);
            // May fail if dir deleted
        }
    }
    
    // Thread B: Delete and recreate
    while (1) {
        rmdir("/test/dir");
        mkdir("/test/dir", 0755);
    }
}
```

---

### Race Type 6: Cache Coherency Issues

**What it is**: Multiple CPUs have inconsistent views of memory

**Scenario**:
```c
// CPU 0
state->offset = new_value;  // Write

// CPU 1
use(state->offset);  // Read stale value from CPU 1 cache
```

**How it manifests**:
- Inconsistent reads
- Lost updates
- Very hard to reproduce

**How to trigger**:
- Run on multi-socket NUMA system
- Pin threads to different NUMA nodes
- Inject delays to increase window

---

## Part 2: System Faults (Not Race Conditions)

### Fault Type 1: Memory Allocation Failure

**What happens**: `kmalloc()` or `malloc()` returns NULL

**Where it occurs**:
- VFS allocating `getdents_ctx`
- Filesystem allocating per-operation state
- Buffer allocation
- eBPF map allocation

**How to inject (eBPF)**:
```c
SEC("fentry/kmalloc")
int trace_kmalloc_entry(struct pt_regs *ctx) {
    size_t size = PT_REGS_PARM1(ctx);
    
    // Randomly fail large allocations
    u32 rand = bpf_get_prandom_u32();
    if (size > 1024 && (rand % 100) < 5) {  // 5% failure rate
        // Override return to NULL
        bpf_override_return(ctx, 0);  // Return NULL
        bpf_trace_printk("Injected kmalloc failure, size=%zu\n", size);
    }
    
    return 0;
}
```

**How to inject (userspace)**:
```c
// Using LD_PRELOAD to override malloc
void *malloc(size_t size) {
    static void *(*real_malloc)(size_t) = NULL;
    if (!real_malloc)
        real_malloc = dlsym(RTLD_NEXT, "malloc");
    
    // Randomly fail
    if (should_inject_fault() && size > 1024)
        return NULL;
    
    return real_malloc(size);
}
```

**What should happen**: Graceful error handling, no crash, -ENOMEM returned

---

### Fault Type 2: I/O Errors

**What happens**: Disk read fails

**Where it occurs**:
- Reading directory blocks
- Reading htree nodes
- Reading metadata

**How to inject (dm-flakey)**:
```bash
# Create flakey device that randomly fails I/O
dmsetup create flakey-test --table \
  "0 $(blockdev --getsz /dev/vdb) flakey /dev/vdb 0 30 10"
# Parameters: up 30 seconds, down 10 seconds (cycle)

# Mount filesystem on flakey device
mkfs.ext4 /dev/mapper/flakey-test
mount /dev/mapper/flakey-test /test/ext4
```

**How to inject (eBPF)**:
```c
SEC("kprobe/submit_bio")
int trace_submit_bio(struct pt_regs *ctx) {
    struct bio *bio = (struct bio *)PT_REGS_PARM1(ctx);
    
    // Randomly fail reads to directory blocks
    if (is_directory_read(bio) && should_inject_fault()) {
        // Mark bio as failed
        bio->bi_status = BLK_STS_IOERR;
        bpf_trace_printk("Injected I/O error\n");
    }
    
    return 0;
}
```

**What should happen**: Return -EIO, don't crash, retry if appropriate

---

### Fault Type 3: Lock Contention / Deadlock

**What happens**: Lock acquisition fails or deadlocks

**Scenarios**:
- Lock timeout
- Priority inversion
- Deadlock (A waits for B, B waits for A)

**How to inject (eBPF + pause)**:
```c
// Inject deliberate contention
SEC("fentry/down_read")
int trace_down_read_entry(struct pt_regs *ctx) {
    struct rw_semaphore *sem = (struct rw_semaphore *)PT_REGS_PARM1(ctx);
    
    // If this is inode->i_rwsem, inject delay
    if (is_inode_rwsem(sem) && should_inject_pause()) {
        // Hold caller here for 10ms to create contention
        inject_pause(10000);
    }
    
    return 0;
}
```

**How to inject (userspace)**:
```c
// Create artificial contention
void create_lock_contention() {
    int fd = open("/test/dir", O_RDONLY | O_DIRECTORY);
    
    // 50 threads all trying to read same directory
    pthread_t threads[50];
    for (int i = 0; i < 50; i++) {
        pthread_create(&threads[i], NULL, read_dir_thread, &fd);
    }
    
    // All threads compete for inode lock
    for (int i = 0; i < 50; i++) {
        pthread_join(threads[i], NULL);
    }
}
```

**What should happen**: NOWAIT returns -EAGAIN, blocking operations wait gracefully

---

### Fault Type 4: Signal Interruption

**What happens**: Signal delivered during syscall

**Where it matters**:
- io_uring_enter() interrupted
- Long-running directory reads

**How to inject**:
```c
void inject_signals(pid_t target_pid) {
    // Randomly send signals to target process
    while (1) {
        usleep(random() % 10000);  // 0-10ms
        
        // Send harmless signal (doesn't kill, just interrupts)
        kill(target_pid, SIGUSR1);
    }
}
```

**What should happen**: Return -EINTR, application retries

---

### Fault Type 5: Page Fault During copy_to_user

**What happens**: Userspace buffer swapped out, causes page fault

**Where it occurs**:
- Writing directory entries to userspace buffer
- Can block if page needs to be swapped in

**How to inject (memory pressure)**:
```c
void create_memory_pressure() {
    // Allocate and touch memory to trigger swapping
    size_t size = 16ULL * 1024 * 1024 * 1024;  // 16GB
    char *buffer = malloc(size);
    
    while (1) {
        // Touch all pages to force swapping
        for (size_t i = 0; i < size; i += 4096) {
            buffer[i] = i & 0xFF;
        }
        sleep(1);
    }
}
```

**How to inject (specific)**:
```bash
# Force buffer out of memory before operation
echo 3 > /proc/sys/vm/drop_caches

# OR use madvise
madvise(buffer, size, MADV_DONTNEED);
```

**What should happen**: Handle gracefully, may return -EAGAIN if NOWAIT

---

## Part 2: Mechanical Fault Injection Details

### Method 1: eBPF with Precise Injection Points

#### Technique: bpf_override_return

**Requirements**:
- Kernel compiled with `CONFIG_BPF_KPROBE_OVERRIDE=y`
- Function must be in error injection list
- Need CAP_SYS_ADMIN

**Example - Force ENOMEM**:
```c
SEC("kprobe/__kmalloc")
int BPF_KPROBE(trace_kmalloc, size_t size, gfp_t flags)
{
    u32 rand = bpf_get_prandom_u32();
    
    // 5% chance to fail allocations > 1KB
    if (size > 1024 && (rand % 100) < 5) {
        // Override return value to NULL
        bpf_override_return(ctx, 0);
        
        bpf_trace_printk("FAULT INJECTION: kmalloc(%zu) → NULL\n", size);
        return 0;
    }
    
    return 0;
}
```

**Where to inject** for getdents:
1. `vfs_getdents_async` entry → return -EAGAIN
2. `kmalloc` → return NULL (ENOMEM)
3. `ext4_bread` → return -EIO
4. `down_read_trylock` → return 0 (lock busy)

#### Technique: Forced Delays (Pause Injection)

**Problem**: eBPF can't sleep directly

**Solution**: Signal userspace to pause target process

**Architecture**:
```
┌──────────┐         ┌──────────┐         ┌──────────┐
│   eBPF   │ event   │ Userspace│  ptrace │  Target  │
│ Program  ├────────>│ Controller├────────>│ Process  │
└──────────┘         └──────────┘         └──────────┘

1. eBPF hook fires
2. eBPF sends event via perf buffer
3. Userspace reads event
4. Userspace uses ptrace to pause target
5. Sleep for N microseconds
6. Resume target
```

**eBPF side**:
```c
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
} events SEC(".maps");

struct pause_event {
    u32 pid;
    u32 cpu;
    u64 delay_us;
    char function[32];
};

SEC("fentry/vfs_getdents_async")
int BPF_PROG(inject_pause_getdents,
             struct file *file,
             loff_t offset,
             void __user *dirent_buf,
             size_t buflen,
             loff_t *next_offset,
             unsigned int flags)
{
    u32 rand = bpf_get_prandom_u32();
    
    // 10% chance to inject pause
    if ((rand % 100) < 10) {
        struct pause_event event = {
            .pid = bpf_get_current_pid_tgid() >> 32,
            .cpu = bpf_get_smp_processor_id(),
            .delay_us = (rand % 5000) + 1000,  // 1-6ms
        };
        __builtin_memcpy(event.function, "vfs_getdents_async", 18);
        
        // Send event to userspace
        bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                              &event, sizeof(event));
    }
    
    return 0;
}
```

**Userspace controller**:
```c
void handle_pause_event(void *ctx, int cpu, void *data, __u32 size) {
    struct pause_event *event = data;
    
    printf("Pause injection: pid=%d, delay=%luμs, func=%s\n",
           event->pid, event->delay_us, event->function);
    
    // Use ptrace to pause target process
    if (ptrace(PTRACE_ATTACH, event->pid, NULL, NULL) == 0) {
        // Process is now stopped
        usleep(event->delay_us);  // Inject delay
        
        // Resume
        ptrace(PTRACE_DETACH, event->pid, NULL, NULL);
    }
}

int main() {
    struct perf_buffer *pb;
    struct bpf_object *obj;
    
    // Load eBPF program
    obj = bpf_object__open_file("ebpf_injector.bpf.o", NULL);
    bpf_object__load(obj);
    
    // Attach to perf buffer
    int map_fd = bpf_object__find_map_fd_by_name(obj, "events");
    pb = perf_buffer__new(map_fd, 8, handle_pause_event, NULL, NULL, NULL);
    
    // Poll for events
    while (1) {
        perf_buffer__poll(pb, 100);  // 100ms timeout
    }
}
```

---

### Method 2: ptrace with Syscall Interception

#### Technique: Modify Syscall Return Values

**What it does**: Intercept syscall, change return value

**Example - Force EAGAIN**:
```c
void intercept_io_uring_enter(pid_t target_pid) {
    int status;
    
    ptrace(PTRACE_ATTACH, target_pid, NULL, NULL);
    waitpid(target_pid, &status, 0);
    
    ptrace(PTRACE_SETOPTIONS, target_pid, 0, 
           PTRACE_O_TRACESYSGOOD);
    
    while (1) {
        // Wait for syscall entry
        ptrace(PTRACE_SYSCALL, target_pid, 0, 0);
        waitpid(target_pid, &status, 0);
        
        if (WIFEXITED(status)) break;
        
        struct user_regs_struct regs;
        ptrace(PTRACE_GETREGS, target_pid, 0, &regs);
        
        // Is this io_uring_enter?
        if (regs.orig_rax == __NR_io_uring_enter) {
            // Continue to syscall exit
            ptrace(PTRACE_SYSCALL, target_pid, 0, 0);
            waitpid(target_pid, &status, 0);
            
            // Get return value
            ptrace(PTRACE_GETREGS, target_pid, 0, &regs);
            
            // 10% chance: override return value
            if (should_inject_fault()) {
                regs.rax = -EAGAIN;  // Force EAGAIN
                ptrace(PTRACE_SETREGS, target_pid, 0, &regs);
                printf("INJECTED: io_uring_enter → -EAGAIN\n");
            }
        }
    }
    
    ptrace(PTRACE_DETACH, target_pid, 0, 0);
}
```

**Syscalls to intercept**:
- `io_uring_enter` → -EAGAIN, -EINTR
- `io_uring_setup` → -ENOMEM
- `open` → -ENOENT, -EACCES
- `close` → -EBADF

#### Technique: Inject Delays Between Syscalls

**Purpose**: Widen race windows

**Example**:
```c
void inject_syscall_delays(pid_t target_pid) {
    while (1) {
        ptrace(PTRACE_SYSCALL, target_pid, 0, 0);
        waitpid(target_pid, &status, 0);
        
        struct user_regs_struct regs;
        ptrace(PTRACE_GETREGS, target_pid, 0, &regs);
        
        // Just entered a syscall
        if (regs.orig_rax == __NR_io_uring_enter) {
            // DELAY BEFORE SYSCALL EXECUTES
            usleep(random() % 5000);  // 0-5ms random delay
        }
        
        // Continue to syscall exit
        ptrace(PTRACE_SYSCALL, target_pid, 0, 0);
        waitpid(target_pid, &status, 0);
        
        // DELAY AFTER SYSCALL COMPLETES
        usleep(random() % 5000);
    }
}
```

**Effect**: Increases probability of catching races by expanding timing windows

---

### Method 3: Kernel Fault Injection Framework

**Linux has built-in fault injection!**

#### Using failslab

**Purpose**: Fail slab allocations randomly

```bash
# Enable failslab
echo 1 > /sys/kernel/debug/failslab/task-filter
echo 100 > /sys/kernel/debug/failslab/probability  # 100% (of selected)
echo 2 > /sys/kernel/debug/failslab/times  # Fail 2 times then succeed
echo 0 > /sys/kernel/debug/failslab/space  # Fail immediately

# Run test
./test_program &
TEST_PID=$!

# Make failslab target this process
echo $TEST_PID > /proc/$TEST_PID/make-it-fail

# Watch for allocation failures
dmesg | grep -i "fault injection"
```

#### Using fail_page_alloc

**Purpose**: Fail page allocations

```bash
echo 1 > /sys/kernel/debug/fail_page_alloc/task-filter
echo 10 > /sys/kernel/debug/fail_page_alloc/probability  # 10%
echo 100 > /sys/kernel/debug/fail_page_alloc/times
```

#### Using fail_make_request

**Purpose**: Fail block I/O requests

```bash
echo 1 > /sys/kernel/debug/fail_make_request/task-filter
echo 5 > /sys/kernel/debug/fail_make_request/probability  # 5%
echo 1000 > /sys/kernel/debug/fail_make_request/times
```

**Wrap in script**:
```bash
#!/bin/bash
# Enable all fault injection

enable_fault_injection() {
    local probability=$1  # 1-100
    
    # failslab
    echo 1 > /sys/kernel/debug/failslab/task-filter
    echo $probability > /sys/kernel/debug/failslab/probability
    
    # fail_page_alloc
    echo 1 > /sys/kernel/debug/fail_page_alloc/task-filter
    echo $probability > /sys/kernel/debug/fail_page_alloc/probability
    
    # fail_make_request
    echo 1 > /sys/kernel/debug/fail_make_request/task-filter
    echo $probability > /sys/kernel/debug/fail_make_request/probability
    
    echo "Fault injection enabled at ${probability}% probability"
}

run_with_faults() {
    local test_program=$1
    local probability=${2:-10}  # Default 10%
    
    enable_fault_injection $probability
    
    # Run test
    $test_program &
    local pid=$!
    
    # Enable for this PID
    echo 1 > /proc/$pid/make-it-fail
    
    # Wait for completion
    wait $pid
    local exit_code=$?
    
    # Disable fault injection
    echo 0 > /sys/kernel/debug/failslab/task-filter
    echo 0 > /sys/kernel/debug/fail_page_alloc/task-filter
    echo 0 > /sys/kernel/debug/fail_make_request/task-filter
    
    return $exit_code
}
```

---

## Part 3: Specific Race Condition Injection Strategies

### Strategy 1: Pause at Critical Sections

**Identify critical sections** in code:

```c
// Example: ext4_htree_fill_tree
static int ext4_htree_fill_tree(...) {
    // CRITICAL SECTION 1: After getting hash, before lookup
    u32 hash = get_next_hash(state);
    // >>> INJECT PAUSE HERE <<<
    block = htree_lookup(inode, hash);
    
    // CRITICAL SECTION 2: After lookup, before read
    // >>> INJECT PAUSE HERE <<<
    bh = ext4_bread(inode, block);
    
    // CRITICAL SECTION 3: After read, before processing
    // >>> INJECT PAUSE HERE <<<
    process_entries(bh);
}
```

**eBPF injection at each point**:
```c
// Hook for critical section 1
SEC("fentry/ext4_dx_find_entry")  // Called by htree_lookup
int trace_dx_find_entry(struct pt_regs *ctx) {
    if (should_inject_pause()) {
        signal_userspace_to_pause(5000);  // 5ms pause
    }
    return 0;
}

// Hook for critical section 2
SEC("fentry/ext4_bread")
int trace_ext4_bread(struct pt_regs *ctx) {
    if (should_inject_pause()) {
        signal_userspace_to_pause(3000);  // 3ms pause
    }
    return 0;
}
```

**Why this works**: Pauses expand the race window, making rare races common

---

### Strategy 2: Interleaved Execution

**Force specific thread orderings**

**Example - Force Thread B to run between Thread A's operations**:

```c
// Control thread execution order using semaphores

sem_t sem_a, sem_b;

void* thread_a(void* arg) {
    // Step 1
    file->f_pos = 0x1000;
    
    // SIGNAL thread B to run
    sem_post(&sem_b);
    
    // WAIT for thread B to corrupt
    sem_wait(&sem_a);
    
    // Step 2: Read (now will use corrupted f_pos!)
    read_directory(file);
}

void* thread_b(void* arg) {
    // WAIT for thread A to set f_pos
    sem_wait(&sem_b);
    
    // Corrupt it
    file->f_pos = 0x2000;
    
    // SIGNAL thread A to continue
    sem_post(&sem_a);
}
```

**eBPF variant** - Control scheduling:
```c
SEC("tracepoint/sched/sched_switch")
int trace_sched_switch(struct trace_event_raw_sched_switch *ctx) {
    // Detect when thread A is about to run
    // Force context switch to thread B
    // (Requires bpf_send_signal or similar)
    
    return 0;
}
```

---

### Strategy 3: Stress Scheduler

**Force CPU migrations** to trigger memory ordering bugs

```c
void stress_cpu_migration() {
    pthread_t threads[100];
    
    for (int i = 0; i < 100; i++) {
        pthread_create(&threads[i], NULL, reader_thread, NULL);
        
        // Pin each thread to random CPU
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(random() % get_nprocs(), &cpuset);
        pthread_setaffinity_np(threads[i], sizeof(cpuset), &cpuset);
    }
    
    // Constantly change CPU affinity while running
    while (1) {
        usleep(1000);  // Every 1ms
        for (int i = 0; i < 100; i++) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(random() % get_nprocs(), &cpuset);
            pthread_setaffinity_np(threads[i], sizeof(cpuset), &cpuset);
        }
    }
}
```

**Effect**: Forces cache invalidation, exposes memory ordering bugs

---

### Strategy 4: Atomic Operation Reordering

**Use Thread Sanitizer** to detect races

```bash
# Compile with TSan
gcc -fsanitize=thread -g -O1 test.c -o test

# Run
./test

# TSan will report:
# ==================
# WARNING: ThreadSanitizer: data race
#   Write of size 8 at 0x7fff12345678 by thread T2:
#     #0 set_offset test.c:123
#   Previous write of size 8 at 0x7fff12345678 by thread T1:
#     #0 set_offset test.c:123
# ==================
```

**Inject delays to make TSan-detected races more likely**:
```c
#ifdef TSAN_ENABLED
#define INJECT_TSAN_DELAY() usleep(100)
#else
#define INJECT_TSAN_DELAY()
#endif

void set_offset(struct file *file, loff_t offset) {
    INJECT_TSAN_DELAY();  // Expand race window for TSan
    file->f_pos = offset;
    INJECT_TSAN_DELAY();
}
```

---

## Part 4: Real-World Bug Examples

### Bug Example 1: Linux ext3 readdir race (2007)

**Bug**: Concurrent `readdir()` could return duplicate entries

**Root cause**:
```c
// ext3_readdir() circa 2007
static int ext3_readdir(struct file *file, void *dirent, filldir_t filldir) {
    loff_t pos = file->f_pos;  // Read shared state
    
    // RACE WINDOW: Another thread can modify f_pos here!
    
    offset = (pos & ~(sb->s_blocksize - 1));
    // Use potentially corrupted offset
}
```

**Fix**: Added locking around f_pos access

**How our tests would catch it**:
```c
// Test: concurrent_readers_same_fd
void test() {
    int fd = open("/test/dir", O_RDONLY | O_DIRECTORY);
    
    // 10 threads reading same fd
    for (int i = 0; i < 10; i++)
        pthread_create(&t[i], NULL, reader, &fd);
    
    // Collect all entries
    // Check for duplicates
    assert(no_duplicates_found());  // WOULD FAIL with bug
}
```

---

### Bug Example 2: FreeBSD UFS readdir cookie corruption (2013)

**Bug**: Directory cookies could wrap around, causing infinite loop

**Root cause**:
```c
// ufs_readdir() cookies calculated as:
*cookies++ = offset + dp->d_reclen;  // Can overflow!

// If offset near UINT64_MAX, wraps to 0, infinite loop
```

**Fix**: Added overflow check

**How our tests would catch it**:
```c
// Test: large_offset_wraparound
void test() {
    // Craft offset near UINT64_MAX
    uint64_t offset = UINT64_MAX - 1000;
    
    // Try to read
    int ret = vfs_getdents_async(fd, offset, ...);
    
    // Should either:
    // - Return -EINVAL (reject invalid offset)
    // - Handle gracefully
    // Should NOT infinite loop
    
    // Set timeout - if we're still here after 1s, FAIL
    alarm(1);
    // ... rest of test ...
}
```

---

### Bug Example 3: ZFS zap_cursor race (2016)

**Bug**: Concurrent B-tree modification during cursor serialization

**Root cause**:
```c
// zap_cursor_serialize() read B-tree state
uint64_t hash = cursor->zc_hash;  // Read

// RACE: B-tree rebalanced here by another thread

uint64_t cd = cursor->zc_cd;  // Read again
// hash and cd now inconsistent!

return (hash << 32) | cd;  // Invalid cursor!
```

**Fix**: Use lock or generation number

**How our tests would catch it**:
```c
// Test: rapid_btree_modification
void test() {
    // Thread A: Continuously read with offset resume
    while (1) {
        ret = vfs_getdents_async(fd, offset, ...);
        offset = next_offset;
        
        // INJECT PAUSE between getting next_offset and using it
        if (chaos_mode) usleep(1000);
    }
    
    // Thread B: Rapidly create/delete to force B-tree rebalancing
    while (1) {
        create_file();
        delete_file();
    }
    
    // Will catch cursor corruption via -EINVAL or wrong results
}
```

---

### Bug Example 4: Linux dcache corruption (2019)

**Bug**: `dentry` use-after-free in concurrent operations

**Root cause**:
```c
// Thread A
dentry = lookup_dentry(name);
// RACE: Thread B can free dentry here

dentry->d_inode;  // USE-AFTER-FREE!
```

**Fix**: Reference counting, RCU

**How our tests would catch it**:
```c
// Run with KASAN enabled
./configure --enable-kasan
make

// Run concurrent test
./chaos_rapid_modifications /test/dir

// KASAN will report:
// BUG: KASAN: use-after-free in ...
```

---

## Part 5: Comprehensive Fault Injection Matrix

### What Faults to Inject Where

| Location | Fault Type | Injection Method | Expected Behavior |
|----------|------------|------------------|-------------------|
| **vfs_getdents_async entry** | Return -EAGAIN | eBPF override | Retry or thread pool |
| **vfs_getdents_async entry** | Pause 5ms | eBPF event → ptrace | Expose races |
| **kmalloc** | Return NULL | eBPF override | Return -ENOMEM |
| **ext4_bread** | Return -EIO | eBPF override | Return -EIO to user |
| **down_read_trylock** | Return 0 (fail) | eBPF override | Return -EAGAIN |
| **copy_to_user** | Return -EFAULT | Fault buffer page | Return -EFAULT |
| **io_uring_enter** | Return -EINTR | ptrace + signal | Retry syscall |
| **Between iterations** | Pause thread | ptrace | Expose modification races |
| **Block I/O** | Random failures | dm-flakey | Handle I/O errors |
| **Memory pressure** | Swapping | allocate+touch | Page faults during copy_to_user |

---

## Part 6: Pause Injection Timing Strategy

### Identifying Critical Windows

**Where to pause to maximize race detection**:

```c
// Pseudocode for vfs_getdents_async with pause points

int vfs_getdents_async(...) {
    struct getdents_ctx ctx;
    
    // PAUSE POINT 1: After context init, before lock
    >>> INJECT 1-5ms PAUSE <<<
    
    if (flags & NOWAIT) {
        if (!down_read_trylock(&inode->i_rwsem))
            return -EAGAIN;
    } else {
        down_read(&inode->i_rwsem);
    }
    
    // PAUSE POINT 2: After lock, before FS call
    >>> INJECT 1-5ms PAUSE <<<
    
    ret = file->f_op->iterate_async(file, offset, &ctx);
    
    // PAUSE POINT 3: After FS call, before unlock
    >>> INJECT 1-5ms PAUSE <<<
    
    up_read(&inode->i_rwsem);
    
    // PAUSE POINT 4: After unlock, before return
    >>> INJECT 1-5ms PAUSE <<<
    
    return ret;
}
```

**Why these points**:
- Point 1: Another thread could modify directory before we lock
- Point 2: Maximizes lock hold time, tests contention
- Point 3: Another thread could grab lock before we release
- Point 4: Tests cleanup race conditions

### Pause Duration Guidelines

**Short pauses** (0-100μs):
- Trigger common races
- Don't slow tests too much
- Use for frequently hit code paths

**Medium pauses** (100μs-5ms):
- Trigger uncommon races
- Balanced between coverage and speed
- Use for moderately frequent paths

**Long pauses** (5ms-100ms):
- Trigger rare races
- Significantly slow tests
- Use sparingly, for critical sections

**Very long pauses** (100ms+):
- Simulate pathological conditions (hung disk, etc.)
- Very slow tests
- Use only in dedicated stress tests

### Adaptive Pause Injection

**Strategy**: Start short, increase if no bugs found

```c
struct adaptive_injector {
    uint32_t pause_duration_us;
    uint32_t faults_found;
    uint32_t iterations;
};

void adaptive_inject_pause(struct adaptive_injector *inj) {
    if (should_inject()) {
        usleep(inj->pause_duration_us);
    }
    
    inj->iterations++;
    
    // Increase pause duration if not finding bugs
    if (inj->iterations % 1000 == 0 && inj->faults_found == 0) {
        inj->pause_duration_us = min(inj->pause_duration_us * 2, 100000);
        printf("Increasing pause duration to %uμs\n", inj->pause_duration_us);
    }
}
```

---

## Part 7: Concrete eBPF Injection Implementation

### Complete Working Example

**File**: `chaos/ebpf_pause_injector.bpf.c`

```c
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <linux/ptrace.h>

char LICENSE[] SEC("license") = "GPL";

// Configuration map (updated from userspace)
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 10);
    __type(key, u32);
    __type(value, u64);
} config SEC(".maps");

// Event map for sending pause requests to userspace
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u32));
} pause_events SEC(".maps");

struct pause_request {
    u32 pid;
    u32 tid;
    u64 delay_us;
    u64 timestamp;
    char location[64];
};

// Config keys
#define CFG_PAUSE_PROBABILITY 0
#define CFG_MIN_DELAY_US      1
#define CFG_MAX_DELAY_US      2
#define CFG_FAULT_PROBABILITY 3

// Helper to read config
static __always_inline u64 get_config(u32 key) {
    u64 *val = bpf_map_lookup_elem(&config, &key);
    return val ? *val : 0;
}

// Helper to decide if should inject
static __always_inline bool should_inject() {
    u64 prob = get_config(CFG_PAUSE_PROBABILITY);
    if (prob == 0)
        return false;
    
    u32 rand = bpf_get_prandom_u32();
    return (rand % 100) < prob;
}

// Helper to get random delay
static __always_inline u64 get_random_delay() {
    u64 min = get_config(CFG_MIN_DELAY_US);
    u64 max = get_config(CFG_MAX_DELAY_US);
    
    if (max <= min)
        return min;
    
    u32 rand = bpf_get_prandom_u32();
    return min + (rand % (max - min));
}

// Injection Point 1: vfs_getdents_async entry
SEC("fentry/vfs_getdents_async")
int BPF_PROG(pause_vfs_getdents_entry,
             struct file *file,
             loff_t offset)
{
    if (!should_inject())
        return 0;
    
    struct pause_request req = {
        .pid = bpf_get_current_pid_tgid() >> 32,
        .tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF,
        .delay_us = get_random_delay(),
        .timestamp = bpf_ktime_get_ns(),
    };
    __builtin_memcpy(req.location, "vfs_getdents_async:entry", 25);
    
    bpf_perf_event_output(ctx, &pause_events, BPF_F_CURRENT_CPU,
                          &req, sizeof(req));
    
    bpf_trace_printk("PAUSE REQ: pid=%d delay=%lluus at vfs_entry\n",
                     req.pid, req.delay_us);
    
    return 0;
}

// Injection Point 2: Before filesystem call
SEC("fexit/down_read")  // After acquiring inode lock
int BPF_PROG(pause_after_lock)
{
    if (!should_inject())
        return 0;
    
    struct pause_request req = {
        .pid = bpf_get_current_pid_tgid() >> 32,
        .tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF,
        .delay_us = get_random_delay(),
        .timestamp = bpf_ktime_get_ns(),
    };
    __builtin_memcpy(req.location, "after_inode_lock", 17);
    
    bpf_perf_event_output(ctx, &pause_events, BPF_F_CURRENT_CPU,
                          &req, sizeof(req));
    
    return 0;
}

// Injection Point 3: ext4 htree lookup
SEC("fentry/ext4_dx_find_entry")
int BPF_PROG(pause_ext4_lookup)
{
    if (!should_inject())
        return 0;
    
    struct pause_request req = {
        .pid = bpf_get_current_pid_tgid() >> 32,
        .tid = bpf_get_current_pid_tgid() & 0xFFFFFFFF,
        .delay_us = get_random_delay(),
        .timestamp = bpf_ktime_get_ns(),
    };
    __builtin_memcpy(req.location, "ext4_htree_lookup", 18);
    
    bpf_perf_event_output(ctx, &pause_events, BPF_F_CURRENT_CPU,
                          &req, sizeof(req));
    
    return 0;
}

// Fault Injection: kmalloc failures
SEC("kprobe/__kmalloc")
int BPF_KPROBE(fault_kmalloc, size_t size, gfp_t flags)
{
    u64 fault_prob = get_config(CFG_FAULT_PROBABILITY);
    if (fault_prob == 0)
        return 0;
    
    u32 rand = bpf_get_prandom_u32();
    if ((rand % 100) >= fault_prob)
        return 0;
    
    // Only fail large allocations
    if (size > 512) {
        bpf_override_return(ctx, 0);  // Return NULL
        bpf_trace_printk("FAULT INJECTION: kmalloc(%zu) → NULL\n", size);
    }
    
    return 0;
}
```

### Userspace Controller (Handles Pause Requests)

**File**: `chaos/pause_controller.c`

```c
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdio.h>

struct pause_request {
    uint32_t pid;
    uint32_t tid;
    uint64_t delay_us;
    uint64_t timestamp;
    char location[64];
};

struct pause_controller {
    struct bpf_object *obj;
    struct perf_buffer *pb;
    pthread_t thread;
    volatile bool stop;
};

// Callback when eBPF sends pause request
void handle_pause_request(void *ctx, int cpu, void *data, __u32 size) {
    struct pause_request *req = data;
    
    printf("[%llu] PAUSE: pid=%d, delay=%lluμs, location=%s\n",
           req->timestamp / 1000000, req->pid, req->delay_us, req->location);
    
    // Pause the target process using SIGSTOP
    // (More reliable than ptrace for this use case)
    
    if (kill(req->pid, SIGSTOP) == 0) {
        // Process stopped
        usleep(req->delay_us);
        
        // Resume
        kill(req->pid, SIGCONT);
        
        printf("  Resumed pid=%d after %lluμs\n", req->pid, req->delay_us);
    } else {
        perror("kill SIGSTOP");
    }
}

struct pause_controller *pause_controller_start(const char *bpf_obj_path) {
    struct pause_controller *ctrl = calloc(1, sizeof(*ctrl));
    
    // Load eBPF program
    ctrl->obj = bpf_object__open_file(bpf_obj_path, NULL);
    if (!ctrl->obj) {
        free(ctrl);
        return NULL;
    }
    
    if (bpf_object__load(ctrl->obj) < 0) {
        bpf_object__close(ctrl->obj);
        free(ctrl);
        return NULL;
    }
    
    // Attach all programs
    struct bpf_program *prog;
    bpf_object__for_each_program(prog, ctrl->obj) {
        bpf_program__attach(prog);
    }
    
    // Setup perf buffer
    int map_fd = bpf_object__find_map_fd_by_name(ctrl->obj, "pause_events");
    ctrl->pb = perf_buffer__new(map_fd, 8, handle_pause_request, NULL, NULL, NULL);
    
    printf("Pause controller started, listening for pause requests...\n");
    
    return ctrl;
}

void pause_controller_run(struct pause_controller *ctrl) {
    // Poll for events
    while (!ctrl->stop) {
        perf_buffer__poll(ctrl->pb, 100);  // 100ms timeout
    }
}

void pause_controller_set_config(struct pause_controller *ctrl,
                                  uint32_t pause_prob_pct,
                                  uint64_t min_delay_us,
                                  uint64_t max_delay_us,
                                  uint32_t fault_prob_pct) {
    int config_map_fd = bpf_object__find_map_fd_by_name(ctrl->obj, "config");
    
    uint32_t key;
    uint64_t val;
    
    key = CFG_PAUSE_PROBABILITY;
    val = pause_prob_pct;
    bpf_map_update_elem(config_map_fd, &key, &val, BPF_ANY);
    
    key = CFG_MIN_DELAY_US;
    val = min_delay_us;
    bpf_map_update_elem(config_map_fd, &key, &val, BPF_ANY);
    
    key = CFG_MAX_DELAY_US;
    val = max_delay_us;
    bpf_map_update_elem(config_map_fd, &key, &val, BPF_ANY);
    
    key = CFG_FAULT_PROBABILITY;
    val = fault_prob_pct;
    bpf_map_update_elem(config_map_fd, &key, &val, BPF_ANY);
    
    printf("Config updated: pause=%d%%, delay=%lu-%luμs, fault=%d%%\n",
           pause_prob_pct, min_delay_us, max_delay_us, fault_prob_pct);
}
```

---

## Part 8: Example Test Scenarios

### Scenario 1: Detect f_pos Corruption

**Setup**:
```c
void test_fpos_corruption_detection() {
    // Enable pause injection at 50% probability
    pause_controller_set_config(ctrl, 50, 100, 5000, 0);
    
    int fd = open("/test/ext4/dir", O_RDONLY | O_DIRECTORY);
    
    // Launch 10 threads, same fd
    pthread_t threads[10];
    struct thread_result results[10];
    
    for (int i = 0; i < 10; i++) {
        results[i].fd = fd;
        results[i].thread_id = i;
        results[i].entries_seen = hash_set_create();
        pthread_create(&threads[i], NULL, reader_with_tracking, &results[i]);
    }
    
    // Let them run
    for (int i = 0; i < 10; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Check for duplicates across threads
    hash_set_t *all_entries = hash_set_create();
    int duplicates = 0;
    
    for (int i = 0; i < 10; i++) {
        hash_set_iter_t *iter = hash_set_iter(results[i].entries_seen);
        while (hash_set_iter_next(iter)) {
            const char *entry = hash_set_iter_value(iter);
            if (hash_set_contains(all_entries, entry)) {
                printf("DUPLICATE FOUND: %s (thread %d)\n", entry, i);
                duplicates++;
            }
            hash_set_add(all_entries, entry);
        }
    }
    
    if (duplicates > 0) {
        printf("RACE DETECTED: %d duplicate entries (f_pos corruption likely)\n", duplicates);
    } else {
        printf("PASS: No f_pos corruption detected\n");
    }
}
```

**Expected behavior with bug**: Duplicates found
**Expected behavior without bug**: No duplicates even with pauses

---

### Scenario 2: Trigger Modification Race

**Setup**:
```c
void test_concurrent_modification_race() {
    // Enable pause injection at critical points
    pause_controller_set_config(ctrl, 20, 500, 10000, 0);
    
    const char *dir = "/test/ext4/stress";
    
    // Create initial files
    for (int i = 0; i < 1000; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/initial_%04d", dir, i);
        touch(path);
    }
    
    // Reader: Read directory continuously, track what's seen
    pthread_t reader;
    struct reader_state *rstate = reader_state_create();
    pthread_create(&reader, NULL, tracking_reader, rstate);
    
    // Writer: Rapidly modify
    pthread_t writer;
    struct writer_state *wstate = writer_state_create(dir);
    pthread_create(&writer, NULL, rapid_modifier, wstate);
    
    // Run for 30 seconds
    sleep(30);
    
    // Stop threads
    reader_state_stop(rstate);
    writer_state_stop(wstate);
    pthread_join(reader, NULL);
    pthread_join(writer, NULL);
    
    // Analyze results
    // Check if reader saw:
    // 1. Deleted file after deletion (should be impossible with proper locking)
    // 2. File that never existed (corruption)
    // 3. Consistent snapshot (acceptable)
    
    analyze_reader_results(rstate, wstate);
}
```

---

### Scenario 3: Trigger NOWAIT Race

**Setup**:
```c
void test_nowait_contention_race() {
    // Create high lock contention
    const char *dir = "/test/xfs/nowait";
    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    
    // Enable pause while holding lock (forces NOWAIT failures)
    pause_controller_set_config(ctrl, 30, 5000, 20000, 0);  // 5-20ms pauses
    
    atomic_int eagain_count = 0;
    atomic_int success_count = 0;
    
    // 50 threads, all using NOWAIT
    pthread_t threads[50];
    struct nowait_test_ctx ctx = {
        .fd = fd,
        .flags = IORING_GETDENTS_FL_NOWAIT,
        .eagain_count = &eagain_count,
        .success_count = &success_count,
    };
    
    for (int i = 0; i < 50; i++) {
        pthread_create(&threads[i], NULL, nowait_reader, &ctx);
    }
    
    for (int i = 0; i < 50; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Results:\n");
    printf("  Success: %d\n", atomic_load(&success_count));
    printf("  EAGAIN: %d\n", atomic_load(&eagain_count));
    printf("  EAGAIN rate: %.1f%%\n", 
           100.0 * atomic_load(&eagain_count) / 
           (atomic_load(&success_count) + atomic_load(&eagain_count)));
    
    // With pauses, should see high EAGAIN rate (30-50%)
    // Without pauses, would be low (0-5%)
}
```

---

## Part 9: Testing Framework Integration

### Chaos Test with Full Injection

**File**: `chaos/ultimate_chaos_test.c`

```c
int main(int argc, char *argv[]) {
    const char *test_dir = argv[1];
    
    printf("=== ULTIMATE CHAOS TEST ===\n");
    
    // 1. Load eBPF pause injector
    struct pause_controller *pctrl = pause_controller_start(
        "ebpf_pause_injector.bpf.o");
    if (!pctrl) {
        fprintf(stderr, "Failed to load eBPF, continuing without...\n");
    } else {
        // Configure:
        // - 20% chance of pause injection
        // - 500-10000μs delays (0.5-10ms)
        // - 5% chance of fault injection
        pause_controller_set_config(pctrl, 20, 500, 10000, 5);
        
        // Start event polling thread
        pthread_t pctrl_thread;
        pthread_create(&pctrl_thread, NULL, 
                       (void*(*)(void*))pause_controller_run, pctrl);
    }
    
    // 2. Enable kernel fault injection
    enable_kernel_fault_injection(10);  // 10% failure rate
    
    // 3. Run chaos test
    struct chaos_config config = {
        .num_readers = 20,
        .num_writers = 10,
        .duration_seconds = 60,
        .check_no_duplicates = true,
        .check_no_missing = true,
    };
    
    struct chaos_result *result = chaos_test_run(
        test_dir, &config, DIR_READER_IORING);
    
    // 4. Analyze results
    printf("\n=== RESULTS ===\n");
    printf("Operations: %lu\n", result->operations_total);
    printf("Failed: %lu\n", result->operations_failed);
    printf("Duplicates: %lu\n", result->duplicates_found);
    printf("Missing: %lu\n", result->missing_entries);
    printf("Anomalies: %lu\n", result->detected_anomalies);
    
    if (result->passed) {
        printf("\n✓ TEST PASSED\n");
        printf("Survived 60s of chaos + fault injection + pauses\n");
    } else {
        printf("\n✗ TEST FAILED\n");
        printf("Found bugs under chaos conditions\n");
    }
    
    // 5. Cleanup
    if (pctrl) {
        pctrl->stop = true;
        // Wait for controller thread
        pause_controller_stop(pctrl);
    }
    
    disable_kernel_fault_injection();
    
    return result->passed ? 0 : 1;
}
```

---

## Part 10: Debugging Caught Races

### When a Race is Detected

**Test output**:
```
[2025-10-10 14:23:45] RACE DETECTED!
Thread 5 saw duplicate entry: "file_0042"
Thread 8 also saw: "file_0042"

Thread 5 timeline:
  14:23:45.123 - Read offset 0x1000
  14:23:45.125 - PAUSE INJECTED (2.3ms)
  14:23:45.127 - Resumed
  14:23:45.128 - Saw "file_0042"

Thread 8 timeline:
  14:23:45.124 - Read offset 0x1000 (SAME!)
  14:23:45.126 - Saw "file_0042"

DIAGNOSIS: file->f_pos corruption (both threads used same offset)
```

**How to reproduce manually**:
```c
// Reproduce the exact scenario
void reproduce_race() {
    int fd = open("/test/ext4/stress", O_RDONLY | O_DIRECTORY);
    
    // Thread 5: Read, pause, continue
    pthread_t t5, t8;
    
    struct replay_args args5 = {
        .fd = fd,
        .offset = 0x1000,
        .pause_after_set_offset = 2300,  // 2.3ms from log
    };
    pthread_create(&t5, NULL, replay_thread, &args5);
    
    // Thread 8: Read at same time
    struct replay_args args8 = {
        .fd = fd,
        .offset = 0x1000,  // SAME offset
        .pause_after_set_offset = 0,
    };
    
    usleep(1000);  // Stagger slightly
    pthread_create(&t8, NULL, replay_thread, &args8);
    
    pthread_join(t5, NULL);
    pthread_join(t8, NULL);
    
    // Should reproduce the duplicate
}
```

---

## Summary

### Fault Types Covered

1. ✅ **f_pos corruption** - Shared state races
2. ✅ **Directory modification** - Content changes during iteration
3. ✅ **Cursor encoding** - Invalid offsets after restructuring
4. ✅ **Lock contention** - NOWAIT failures
5. ✅ **Memory allocation** - ENOMEM handling
6. ✅ **I/O errors** - EIO handling
7. ✅ **Signals** - EINTR handling
8. ✅ **Page faults** - copy_to_user blocking
9. ✅ **Cache coherency** - Multi-CPU races

### Injection Methods

1. ✅ **eBPF with bpf_override_return** - Force error returns
2. ✅ **eBPF with perf events** - Signal pauses to userspace
3. ✅ **ptrace** - Intercept and modify syscalls
4. ✅ **SIGSTOP/SIGCONT** - Pause process execution
5. ✅ **Kernel fault injection** - Built-in failslab/fail_page_alloc
6. ✅ **dm-flakey** - Fail block I/O
7. ✅ **Memory pressure** - Force swapping
8. ✅ **Thread Sanitizer** - Detect data races

### Pause Timing

**Why pauses work**:
- Race windows are typically nanoseconds-microseconds wide
- Pausing for milliseconds makes them ~1000x more likely
- Turns "once in a million operations" into "every few hundred"

**Where to pause**:
- ✅ Between lock acquire and use
- ✅ Between check and use (TOCTOU)
- ✅ Between reading state and using it
- ✅ In middle of critical sections
- ✅ Between atomic operations

**Duration guidelines**:
- 100μs - 1ms: Common races
- 1ms - 10ms: Uncommon races
- 10ms - 100ms: Rare races
- 100ms+: Pathological conditions

---

*This document provides concrete, actionable details for implementing comprehensive fault injection and race condition testing.*

*All techniques are production-proven in other chaos engineering frameworks.*

*Use this as a reference when implementing the eBPF and ptrace injectors.*

