# Jepsen-Inspired Filesystem Testing: Conceptual Design

*Research Document: Mapping Distributed Systems Testing to Kernel Filesystems*
*Version: 1.0*
*Date: October 10, 2025*

## Overview

This document explores **conceptually** how Kyle Kingsbury's Jepsen testing methodology for distributed databases can be adapted for testing concurrent filesystem operations using eBPF fault injection.

**Goal**: Design a testing system that finds concurrency bugs in async directory iteration with the same rigor that Jepsen finds bugs in distributed databases.

**Approach**: Study Jepsen's architecture, extract principles, adapt for kernel domain.

---

## Part 1: Understanding Jepsen's Architecture

### Jepsen's Core Components

Based on Jepsen's design, there are **5 key components**:

```
┌─────────────────────────────────────────────────────┐
│                   Jepsen Controller                  │
│                                                      │
│  ┌─────────────┐  ┌─────────────┐  ┌────────────┐ │
│  │  Generator  │  │   Nemesis   │  │  Checker   │ │
│  │  (clients)  │  │  (faults)   │  │(invariants)│ │
│  └─────────────┘  └─────────────┘  └────────────┘ │
│         │                │                │         │
│         ▼                ▼                ▼         │
│    Operations        Faults          History        │
│    (reads/writes)   (partition)    (validation)     │
└─────────────────────────────────────────────────────┘
         │                │                │
         ▼                ▼                ▼
  ┌─────────────────────────────────────────────┐
  │         Target System (Database)             │
  │  Node 1    Node 2    Node 3    Node 4       │
  └─────────────────────────────────────────────┘
```

**1. Generator**:
- Creates client operations (read, write, CAS)
- Concurrent clients performing operations
- Records operation history (what was attempted, when)

**2. Nemesis**:
- Injects faults into the system
- Network partitions (split brain)
- Node crashes and restarts
- Clock skew
- Packet loss/delay
- Runs concurrently with clients

**3. Checker**:
- Validates operation history
- Checks for linearizability violations
- Detects anomalies (lost updates, stale reads)
- Property-based validation

**4. History**:
- Complete record of all operations
- Includes: operation type, time, result, client ID
- Used for post-test analysis

**5. Scheduler** (implicit):
- Controls timing and concurrency
- Can replay specific scenarios
- Explores different interleavings

---

## Part 2: Jepsen's Fault Model (Nemesis)

### Fault Types in Distributed Systems

**Network faults** (Jepsen's primary domain):
```
Cluster: [Node A] ←→ [Node B] ←→ [Node C]

Nemesis action: Partition
Result: [Node A] ← ✗ → [Node B] ←→ [Node C]
        (A can't talk to B, but B and C can communicate)

Effect: Tests split-brain scenarios, consistency under partition
```

**Process faults**:
- Kill nodes randomly
- Restart nodes
- Pause processes (SIGSTOP)
- Clock skew (change system time)

**Timing faults**:
- Inject network delays
- Slow down specific nodes
- Create asymmetric latency

### Key Insight from Jepsen

**"Nemesis runs concurrently with clients"**

While clients are performing operations, Nemesis is:
- Creating network partitions
- Killing nodes
- Injecting delays
- Creating chaos

**Result**: Exposes race conditions that only occur under specific timing/failure combinations

---

## Part 3: Mapping Jepsen to Filesystem Testing

### Conceptual Translation

| Jepsen Component | Filesystem Analog | Our Implementation |
|------------------|-------------------|-------------------|
| **Distributed Database** | Shared directory on filesystem | ext4/XFS/ZFS directory |
| **Database Nodes** | Concurrent threads/processes | Reader/writer threads |
| **Network** | Kernel synchronization (locks, memory) | VFS layer, inode locks |
| **Network Partition** | Lock contention, cache invalidation | Force lock failures, CPU migration |
| **Client Operations** | Database reads/writes | Directory reads, file create/delete |
| **Nemesis** | Fault injector | eBPF fault injector |
| **History** | Operation log | System call trace + results |
| **Checker** | Linearizability check | Invariant validation (no duplicates) |

### The Key Question

**Jepsen asks**: "Can the database maintain consistency during network partition?"

**We ask**: "Can async getdents maintain correctness during concurrent modifications?"

---

## Part 4: Filesystem-Specific Fault Model

### What Are Our "Network Partitions"?

In distributed systems, network partition prevents communication.

**In filesystem, analogous faults**:

**1. Lock Partitioning**:
- Thread A holds shared lock
- Thread B tries to acquire, gets -EAGAIN (NOWAIT)
- Similar to "can't reach other node"

**2. Cache Partitioning**:
- CPU 0 has data in L1 cache
- CPU 1 needs same data, cache miss
- Delay while fetching from RAM
- Similar to network latency

**3. Memory Visibility**:
- Thread A writes to `state->offset`
- Thread B reads `state->offset`
- Without memory barrier, may see stale value
- Similar to "partial partition" (some updates lost)

**4. Filesystem State Divergence**:
- Reader has cursor pointing to block 10
- Writer modifies directory, block 10 now contains different data
- Reader sees "inconsistent view"
- Similar to "split brain"

### Our Faults (Nemesis Actions)

**Analogous to Jepsen's nemesis actions**:

| Jepsen Nemesis | Filesystem Nemesis | eBPF Implementation Concept |
|----------------|-------------------|----------------------------|
| **Network partition** | Force lock contention | Pause thread while holding lock |
| **Kill node** | Kill reader thread | Send SIGKILL randomly |
| **Pause node** | Pause at critical point | eBPF event → SIGSTOP |
| **Restart node** | Close and reopen directory | Force EBADF, reopen |
| **Clock skew** | N/A (not relevant) | - |
| **Slow network** | Slow I/O | Pause in ext4_bread |
| **Packet loss** | Lost updates | Clear dirty bit, lose write |

---

## Part 5: Conceptual eBPF Nemesis Design

### Architecture Vision

```
┌──────────────────────────────────────────────────────┐
│              Nemesis Controller (Userspace)          │
│                                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────┐ │
│  │   Fault      │  │   History    │  │ Checker  │ │
│  │  Scheduler   │  │   Recorder   │  │(validate)│ │
│  └──────────────┘  └──────────────┘  └──────────┘ │
│         │                  ▲               ▲        │
│         │ config      log  │               │        │
│         ▼                  │               │        │
│  ┌──────────────────────────────────────────────┐  │
│  │         eBPF Nemesis Programs                │  │
│  │  (Loaded into kernel, inject faults)         │  │
│  └──────────────────────────────────────────────┘  │
└─────────────────┬────────────────────────────────────┘
                  │ BPF hooks
                  ▼
      ┌──────────────────────────────┐
      │      Linux Kernel            │
      │  ┌────────┐  ┌────────────┐ │
      │  │  VFS   │  │Filesystems │ │
      │  └────────┘  └────────────┘ │
      └──────────────────────────────┘
                  ▲
                  │ syscalls
                  │
      ┌──────────────────────────────┐
      │      Test Workload           │
      │  ┌──────┐  ┌──────┐         │
      │  │Reader│  │Writer│  ...     │
      │  └──────┘  └──────┘         │
      └──────────────────────────────┘
```

### Component 1: Fault Scheduler (Nemesis Brain)

**Conceptual design**:

```
FaultScheduler:
  Maintains a "fault schedule":
    - Every N milliseconds, decide: inject fault or not?
    - If yes, choose fault type (pause, error, crash)
    - If pause: choose duration (short/medium/long)
    - If error: choose type (ENOMEM, EIO, EAGAIN)
    - If crash: choose target (which thread/process)
  
  Communicates with eBPF:
    - Updates BPF map with current fault probabilities
    - Receives events from eBPF hooks
    - Coordinates pauses via SIGSTOP/SIGCONT
  
  Records all faults:
    - Timestamp
    - Fault type
    - Target (PID/TID)
    - Duration (for pauses)
    - Location (which kernel function)
```

**Conceptual algorithm**:
```
initialize():
    fault_schedule = create_schedule()
    current_phase = "warmup"  // No faults yet
    
loop:
    wait_for_tick()  // Every 10ms
    
    if current_phase == "warmup":
        if elapsed > 5s:
            current_phase = "gentle"
    
    if current_phase == "gentle":
        fault_probability = 0.05  // 5%
        max_pause = 1ms
        if elapsed > 20s:
            current_phase = "aggressive"
    
    if current_phase == "aggressive":
        fault_probability = 0.20  // 20%
        max_pause = 10ms
    
    // Update eBPF configuration
    update_bpf_config(fault_probability, max_pause)
    
    // Record current phase in history
    history.record("phase", current_phase, timestamp)
```

**Why phases?**:
- Warmup: Let system stabilize, establish baseline
- Gentle: Start finding common bugs
- Aggressive: Find rare, subtle bugs
- Allows progression from "does it work?" to "does it survive chaos?"

---

### Component 2: eBPF Hook Points (Nemesis Actuators)

**Conceptual hook placement strategy**:

#### Strategy 1: Hook State Transitions

**Concept**: Insert hooks at every state transition

```
State Machine for directory iteration:

[INIT] → [LOCKED] → [READING] → [UNLOCKED] → [RETURN]
   ↑        ↑           ↑           ↑           ↑
   │        │           │           │           │
  Hook1   Hook2       Hook3       Hook4       Hook5

Hook1 (INIT→LOCKED): Before acquiring lock
  Fault options:
    - Pause to increase contention
    - Force ENOMEM on context allocation
    
Hook2 (LOCKED→READING): After lock acquired, before FS call
  Fault options:
    - Pause while holding lock (blocks other threads)
    - Force lock release (simulate crash)
    
Hook3 (READING): During filesystem iteration
  Fault options:
    - Pause between cursor operations
    - Force -EIO on block read
    - Corrupt offset/cursor value
    
Hook4 (READING→UNLOCKED): After FS call, before unlock
  Fault options:
    - Pause before unlock (maximize lock hold time)
    - Force error return
    
Hook5 (UNLOCKED→RETURN): After unlock, before return to user
  Fault options:
    - Pause to allow another thread to enter
    - Corrupt return value
```

**Why this matters**: Every state transition is a potential race point

#### Strategy 2: Hook Resource Acquisitions

**Concept**: Inject faults when acquiring any resource

```
Resource Types:
1. Locks (mutexes, semaphores, rwlocks)
2. Memory (kmalloc, page allocation)
3. I/O (block reads, metadata reads)
4. File descriptors
5. CPU time (scheduling)

For each resource acquisition:
  Hook points:
    - Before request
    - After request granted
    - Before release
  
  Fault options:
    - Fail acquisition (return error)
    - Delay acquisition (pause)
    - Premature release (simulate crash)
    - Hold longer (delay release)
```

**Example - Lock acquisition**:
```
down_read(&inode->i_rwsem):
  
  Hook: fentry/down_read
    Fault: Pause 10ms BEFORE acquiring
    Effect: Creates artificial contention, tests NOWAIT behavior
  
  Hook: fexit/down_read (successful)
    Fault: Pause 5ms AFTER acquiring, BEFORE continuing
    Effect: Holds lock longer, increases contention for other threads
  
  Hook: fentry/up_read
    Fault: Pause BEFORE releasing
    Effect: Extends critical section
```

#### Strategy 3: Hook Consistency Points

**Concept**: Inject faults where consistency assumptions are made

```
Consistency Assumptions in getdents:

Assumption 1: "Offset encoding is valid"
  Hook: Between decode and use
  Fault: Pause, let writer invalidate offset
  
Assumption 2: "Directory content unchanged since lock"
  Hook: After lock, before first read
  Fault: Pause, writer modifies (if possible)
  
Assumption 3: "Buffer remains valid during copy_to_user"
  Hook: During copy
  Fault: Unmap page (trigger page fault)
  
Assumption 4: "inode metadata consistent"
  Hook: Between metadata read and use
  Fault: Pause, let writer update inode
```

---

### Component 3: History Recording (Jepsen's History)

**Jepsen's history format**:
```clojure
{:type :invoke :process 0 :f :read :value nil :time 0}
{:type :ok :process 0 :f :read :value 42 :time 5}
{:type :invoke :process 1 :f :write :value 43 :time 3}
{:type :ok :process 1 :f :write :value 43 :time 8}
```

**Our filesystem analog**:
```json
{
  "type": "invoke",
  "thread": 0,
  "operation": "getdents",
  "offset": "0x0",
  "flags": "NOWAIT",
  "time_ns": 1234567890
}
{
  "type": "ok",
  "thread": 0,
  "operation": "getdents",
  "entries": [".", "..", "file1", "file2"],
  "next_offset": "0x1000",
  "time_ns": 1234570000
}
{
  "type": "invoke",
  "thread": 1,
  "operation": "create",
  "filename": "file3",
  "time_ns": 1234568000
}
{
  "type": "ok",
  "thread": 1,
  "operation": "create",
  "filename": "file3",
  "time_ns": 1234569000
}
```

**With nemesis events**:
```json
{
  "type": "nemesis",
  "action": "pause",
  "target": {"thread": 0, "pid": 1234},
  "duration_us": 5000,
  "location": "vfs_getdents_async:entry",
  "time_ns": 1234568500
}
```

**Conceptual design for recording**:

```
History Recorder Architecture:

┌─────────────────────────────────────┐
│        History Recorder             │
│                                     │
│  Sources:                           │
│  1. Test threads → Direct logging   │
│  2. eBPF events → Perf buffers      │
│  3. Kernel ftrace → Trace events    │
│  4. Nemesis → Fault events          │
│                                     │
│  All timestamped with:              │
│  - High-resolution clock (ns)       │
│  - CPU ID (detect migrations)       │
│  - Thread ID                        │
│                                     │
│  Output:                            │
│  - JSON stream                      │
│  - One event per line               │
│  - Can be replayed                  │
└─────────────────────────────────────┘
```

**Why this matters**:
- Post-mortem analysis
- Replay specific scenarios
- Visualize timelines
- Detect causality violations

---

### Component 4: Checker (Invariant Validation)

**Jepsen's checkers verify**:
- Linearizability
- Sequential consistency
- Serializability
- Custom properties

**Our filesystem checkers verify**:

#### Checker 1: No Duplicate Entries (Single Scan)

**Invariant**: "In a single directory scan, no entry should appear twice"

**Conceptual algorithm**:
```
For each thread's operation history:
  entries_seen = HashSet()
  
  For each getdents operation:
    For each entry in result:
      if entry in entries_seen:
        VIOLATION: Duplicate entry
        Record: entry, thread, offset, time
      
      entries_seen.add(entry)
```

**Edge cases to consider**:
- Concurrent modifications (may see new file twice if created during scan)
- Multiple scans (duplicate across scans is OK)
- "." and ".." (should only appear once)

#### Checker 2: Consistent Snapshot (Weak)

**Invariant**: "Entries seen should represent a valid directory state that existed at some point"

**Conceptual validation**:
```
Given history of:
  - Directory operations (create, delete)
  - getdents results

Construct possible directory states:
  State 0 (initial): [a, b, c]
  After create(d): [a, b, c, d]
  After delete(b): [a, c, d]
  
Check if getdents result matches ANY valid state:
  - Thread saw [a, c, d] → ✓ Valid (matches final state)
  - Thread saw [a, b, d] → ✓ Valid (matches intermediate state)
  - Thread saw [b, d] → ✗ Invalid (never a valid state)
```

**Challenge**: With concurrent ops, many valid states exist

**Relaxation**: May need to allow "close to valid" due to POSIX semantics

#### Checker 3: Progress (Liveness)

**Invariant**: "Eventually, all operations complete"

**Conceptual check**:
```
For each thread:
  For each getdents operation:
    if time_to_complete > THRESHOLD:
      if not EAGAIN:
        VIOLATION: Possible deadlock or livelock
```

**Thresholds**:
- Normal: < 100ms
- With faults: < 1s
- NOWAIT: < 10ms (should return quickly)

#### Checker 4: Cursor Validity

**Invariant**: "Resuming with next_offset should continue iteration"

**Conceptual check**:
```
For each getdents operation that returned next_offset:
  Next operation should use that offset
  
  Check:
    - Next operation succeeds OR returns -EINVAL
    - If succeeds, doesn't repeat entries (overlap check)
    - If -EINVAL, cursor became invalid (acceptable if directory modified)
```

---

## Part 6: Conceptual eBPF Nemesis Programs

### Nemesis 1: The Pauser

**Concept**: Randomly pause threads at critical points

**Hook points** (conceptual):
```
1. vfs_getdents_async:entry
   - Pauses RIGHT AFTER entering function
   - Tests: Another thread can enter and cause contention
   
2. after inode_lock_acquired
   - Pauses WHILE HOLDING lock
   - Tests: NOWAIT should return -EAGAIN for other threads
   
3. filesystem:iterate_async:entry
   - Pauses at FS boundary
   - Tests: Filesystem-specific races
   
4. before copy_to_user
   - Pauses before copying results
   - Tests: Buffer validity, user memory
   
5. after inode_unlock
   - Pauses AFTER releasing lock
   - Tests: Another thread acquires lock before we return
```

**Configuration parameters**:
```
pause_config:
  enabled: true/false
  probability: 0-100%
  min_duration: 100us
  max_duration: 10ms
  
  adaptive:
    increase_if_no_bugs_found: true
    max_duration_cap: 100ms
  
  location_filter:
    vfs_layer: 20%     # Pause in VFS 20% of time
    ext4_layer: 30%    # Pause in ext4 30% of time
    xfs_layer: 10%     # etc.
```

**Conceptual eBPF program structure**:
```
BPF Map: pause_config
  - Global configuration
  - Updated by userspace

BPF Map: pause_events (perf buffer)
  - Send pause requests to userspace
  - Include: PID, delay, location

BPF Map: pause_stats
  - How many pauses injected
  - Where
  - When

For each hook point:
  1. Read config from pause_config map
  2. Generate random number
  3. If should_pause(probability):
       duration = random(min, max)
       event = {pid, duration, location}
       perf_event_output(pause_events, event)
  4. Continue execution (no blocking in eBPF!)
  
Userspace controller:
  While true:
    event = poll_perf_buffer(pause_events)
    if event:
      pause_thread(event.pid, event.duration)
```

---

### Nemesis 2: The Fault Injector

**Concept**: Randomly return errors from kernel functions

**Hook points** (conceptual):
```
1. __kmalloc
   - Randomly return NULL
   - Tests: -ENOMEM handling
   
2. ext4_bread
   - Randomly return -EIO
   - Tests: I/O error handling
   
3. down_read_trylock
   - Randomly return 0 (fail)
   - Tests: NOWAIT -EAGAIN path
   
4. copy_to_user
   - Randomly return -EFAULT
   - Tests: Bad buffer handling
```

**Configuration**:
```
fault_config:
  memory_faults:
    enabled: true
    probability: 5%
    min_size_to_fail: 512 bytes
  
  io_faults:
    enabled: true
    probability: 2%
    only_directory_blocks: true
  
  lock_faults:
    enabled: true
    probability: 10%
    only_nowait_locks: true
```

**Conceptual eBPF program**:
```
For kmalloc:
  BPF_KPROBE(__kmalloc, size, flags):
    if config.memory_faults.enabled:
      if size >= config.min_size_to_fail:
        if random() % 100 < config.probability:
          bpf_override_return(ctx, 0)  // Return NULL
          record_fault("kmalloc_null", size, ...)

For ext4_bread:
  BPF_KPROBE(ext4_bread, inode, block, ...):
    if config.io_faults.enabled:
      if is_directory_block(inode, block):
        if random() % 100 < config.probability:
          bpf_override_return(ctx, -EIO)
          record_fault("ext4_bread_eio", block, ...)
```

---

### Nemesis 3: The Modifier

**Concept**: Actively modify directory during reads (like Jepsen's write clients)

**Not eBPF** - This is regular userspace threads

**Conceptual design**:
```
ModifierThread:
  actions = [create_file, delete_file, rename_file, modify_file]
  
  While not_stopped:
    action = choose_random(actions)
    
    execute(action):
      if action == create_file:
        name = generate_random_name()
        create(directory / name)
        record_history("create", name, timestamp)
      
      if action == delete_file:
        name = choose_random_existing_file()
        delete(directory / name)
        record_history("delete", name, timestamp)
    
    pause_random(0-1ms)  // Create rapid churn
```

**Coordination with readers**:
```
Modifier and Readers run concurrently:

Time 0: Reader 1 starts reading [offset=0]
Time 1: Modifier creates "new_file"
Time 2: Modifier deletes "old_file"
Time 3: Reader 1 continues [offset=0x1000]
Time 4: Reader 2 starts reading [offset=0]
Time 5: Modifier creates "another_file"
...

History records all operations
Checker validates: 
  - No crashes
  - No corruption
  - Acceptable to miss "new_file" (created during scan)
  - Acceptable to see "old_file" (deleted during scan)
  - NOT acceptable to see non-existent file
```

---

### Nemesis 4: The Terminator

**Concept**: Randomly kill processes/threads

**Conceptual design**:
```
TerminatorNemesis:
  targets = [reader_threads, writer_threads]
  
  Every N seconds:
    if random() < kill_probability:
      target = choose_random(targets)
      
      kill(target.pid, SIGKILL)
      
      record_history("killed", target.id, timestamp)
      
      // Restart if policy says so
      if should_restart:
        new_thread = spawn_replacement(target)
        targets.add(new_thread)
```

**What this tests**:
- Cleanup on abnormal termination
- Resource leaks (open fds, allocated memory)
- Orphaned locks
- Partial operations

**eBPF integration** (optional):
```
Hook: exit_to_user_mode (process exiting)
  If this is a test thread:
    Record all unclosed fds
    Record all held locks
    Check for resource leaks
```

---

## Part 7: The History Model (Operation Ordering)

### Jepsen's Insight: Happens-Before Relationships

**Concept**: Model causality between operations

```
Operation History:

Thread 0: [invoke getdents] ──→ [ok, entries=[a,b,c]]
                              ↘
Thread 1:                       [invoke create(d)] ──→ [ok]
                                                      ↘
Thread 2:                                             [invoke getdents] ──→ [ok, entries=[a,b,c,d]]

Question: Is it valid for Thread 0 to not see 'd'?
Answer: YES - Thread 0 started before create(d)

Question: Must Thread 2 see 'd'?
Answer: DEPENDS - If Thread 2's invoke happens-after create(d) ok, then YES
```

**Happens-before in our domain**:
```
Operation A happens-before Operation B if:
  1. Same thread, A completes before B starts (program order)
  2. Different threads, A releases lock before B acquires (sync order)
  3. A writes offset, B reads that offset (data dependency)
  4. Transitive closure of above
```

**Conceptual checker**:
```
For each getdents operation:
  entries_seen = result.entries
  
  For each entry in entries_seen:
    Check: Could this entry exist at this point in time?
    
    If entry is "file_X":
      creation_event = find_creation("file_X")
      deletion_event = find_deletion("file_X")
      
      if deletion_event.time < this_operation.invoke_time:
        # File deleted before we started
        if "file_X" in entries_seen:
          VIOLATION: Saw deleted file
      
      if creation_event.time > this_operation.ok_time:
        # File created after we finished
        if "file_X" in entries_seen:
          VIOLATION: Saw file from the future!
```

---

## Part 8: Conceptual Fault Taxonomy for Filesystems

### Temporal Faults (Timing)

**Concept**: Faults that affect timing, not correctness

**Types**:
1. **Pause**: Delay execution
   - Where: Critical sections
   - Duration: Variable (100us - 100ms)
   - Effect: Expands race windows

2. **Slow I/O**: Delay block reads
   - Where: ext4_bread, xfs_buf_read
   - Duration: 10-100ms
   - Effect: Tests async behavior under slow storage

3. **Lock contention**: Hold locks longer
   - Where: After down_read, before up_read
   - Duration: 5-20ms
   - Effect: Tests NOWAIT, concurrent access

### Spatial Faults (State Corruption)

**Concept**: Faults that corrupt state

**Types**:
1. **Memory corruption**: Change values
   - Where: offset values, cursor state
   - What: Flip bits, set to invalid values
   - Effect: Tests validation logic

2. **Cache invalidation**: Force cache misses
   - Where: CPU cache, page cache
   - How: clflush instruction, drop_caches
   - Effect: Tests NOWAIT under cold cache

3. **Partial writes**: Simulate torn writes
   - Where: 64-bit offset writes on 32-bit boundary
   - How: Write half, pause, write other half
   - Effect: Tests atomic access

### Resource Faults (Availability)

**Concept**: Resources unavailable

**Types**:
1. **Out of memory**: Allocations fail
   - Where: kmalloc, page_alloc
   - Probability: 1-10%
   - Effect: Tests -ENOMEM handling

2. **Out of file descriptors**
   - Where: open() calls
   - How: Exhaust fd limit
   - Effect: Tests -EMFILE handling

3. **Disk full**: No space for metadata
   - Where: Directory expansion
   - How: Fill filesystem
   - Effect: Tests -ENOSPC

### Concurrency Faults (Ordering)

**Concept**: Force specific thread interleavings

**Types**:
1. **Race window expansion**: Pause at specific points
   - Goal: Make rare races common
   - Method: Strategic pauses

2. **CPU migration**: Force context switches
   - Goal: Expose cache coherency bugs
   - Method: sched_setaffinity in loop

3. **Priority inversion**: Low-priority thread holds lock
   - Goal: Test fairness, starvation
   - Method: Set thread priorities

---

## Part 9: The Nemesis Schedule (Conceptual)

### Jepsen's Approach: Probabilistic + Scheduled

Jepsen doesn't just inject faults randomly. It has a **schedule**:

```
Test phases:

Phase 1: Warmup (0-10s)
  - No faults
  - Establish baseline
  - Let caches warm up

Phase 2: Gentle chaos (10-30s)
  - Low fault probability (5%)
  - Short pauses (100us-1ms)
  - Find common bugs

Phase 3: Aggressive chaos (30-60s)
  - High fault probability (20%)
  - Longer pauses (1-10ms)
  - Find rare bugs

Phase 4: Extreme chaos (60-90s)
  - Very high probability (50%)
  - Very long pauses (10-100ms)
  - Find extremely rare bugs

Phase 5: Recovery (90-120s)
  - Gradually reduce faults
  - Test recovery behavior
  - Ensure system still works
```

**Conceptual implementation**:
```
NemesisScheduler:
  timeline = [
    {phase: "warmup", duration: 10s, fault_prob: 0%},
    {phase: "gentle", duration: 20s, fault_prob: 5%},
    {phase: "aggressive", duration: 30s, fault_prob: 20%},
    {phase: "extreme", duration: 30s, fault_prob: 50%},
    {phase: "recovery", duration: 30s, fault_prob: 5% → 0%},
  ]
  
  For each phase:
    start_time = now()
    
    While elapsed < phase.duration:
      update_bpf_config(phase.fault_prob)
      
      if phase == "extreme":
        // Also inject specific patterns
        every 5 seconds:
          inject_killall_readers()  // Kill all, restart
          wait_for_restart()
      
      wait(100ms)  // Scheduling tick
    
    record_phase_transition()
```

---

## Part 10: Conceptual Fault Correlation

### Jepsen Insight: Correlated Faults Are More Dangerous

**Simple faults**: Pause one thread
**Correlated faults**: Pause thread A, THEN immediately pause thread B at related point

**Example in distributed systems**:
```
Jepsen Nemesis:
  1. Partition network
  2. IMMEDIATELY kill leader node
  3. Result: Split brain + leader election during partition
  
This combination is more likely to find bugs than either alone.
```

**Filesystem analog**:
```
Correlated Fault 1: "The Double Pause"
  1. Thread A starts getdents, gets offset=0x1000
  2. eBPF pauses Thread A (5ms)
  3. Thread B starts getdents
  4. eBPF detects Thread B, pauses it too (5ms)
  5. Both resume at nearly same time
  6. Both try to acquire lock
  
  Tests: Lock fairness, contention handling

Correlated Fault 2: "Pause During Modification"
  1. Reader at critical point (between cursor decode and lookup)
  2. eBPF pauses Reader (10ms)
  3. During pause, Writer thread deletes files
  4. Reader resumes with now-invalid cursor
  
  Tests: Cursor validity checking

Correlated Fault 3: "Cache Flush + Lock Contention"
  1. Drop page cache (force cold reads)
  2. IMMEDIATELY start 50 concurrent readers
  3. All hit slow I/O path
  4. All compete for locks
  5. eBPF pauses some threads during lock acquisition
  
  Tests: NOWAIT behavior, queue fairness
```

**Conceptual scheduling**:
```
CorrelatedFaultScheduler:
  fault_patterns = [
    {
      name: "double_pause",
      trigger: "thread_A starts getdents",
      action: [
        pause(thread_A, 5ms),
        wait(1ms),
        pause(thread_B, 5ms)  // IF thread_B also starts
      ]
    },
    {
      name: "pause_then_modify",
      trigger: "reader at critical_section",
      action: [
        pause(reader, 10ms),
        signal_writer_to_modify(),  // Userspace coordination
        wait_for_modification(),
        resume(reader)
      ]
    }
  ]
  
  Execute patterns with probability
```

---

## Part 11: Conceptual History Analysis

### Jepsen's Knossos: Linearizability Checker

**What it does**: Given operation history, check if there exists a linearization

**Linearization**: A total ordering of operations consistent with:
1. Real-time order (if A completes before B starts, A < B in linearization)
2. Sequential specification (behaves like single-threaded)

**For our domain**:

**Sequential specification**:
```
Directory contains: [a, b, c]

Sequential spec:
  getdents() → [a, b, c]  (always same order, always complete)
  
Concurrent spec (weaker):
  getdents() → subset of [a, b, c] OR superset if files added
  Order may vary
  May miss recently added
  May see recently deleted
```

**Conceptual checker**:
```
Given history of operations:
  - getdents(offset=0) → [a, b, c]
  - create(d)
  - getdents(offset=0) → [a, b, c, d]
  - delete(b)
  - getdents(offset=0) → [a, c, d]

Check linearizability:
  Try to find ordering where:
    - All operations appear to happen atomically at some point
    - Order respects happens-before
  
  For directory operations:
    - getdents sees snapshot at SOME point during its execution
    - That snapshot is consistent with create/delete operations
```

**Challenge**: Directory iteration is NOT linearizable (by design!)
- POSIX explicitly allows inconsistency
- We need weaker consistency model

**Our model**: "Weak snapshot consistency"
```
Invariants:
  1. No entry appears twice in single scan
  2. No entry from "future" (created after scan ended)
  3. Deleted entry MAY appear (if deleted during scan)
  4. New entry MAY be missing (if created during scan)
  5. No entries that never existed
```

---

## Part 12: Conceptual Feedback Loop

### Jepsen's Adaptive Testing

**Concept**: Learn from failures, focus testing on weak areas

**Jepsen does**:
```
If bug found:
  1. Record the fault sequence that triggered it
  2. Replay that sequence 100 times (confirm)
  3. Minimize the sequence (find minimal reproducer)
  4. Generate similar sequences (explore nearby)
```

**Our adaptation**:
```
AdaptiveTesting:
  bug_database = []
  
  While testing:
    Run test with current config
    
    If bug found:
      minimal_sequence = minimize_fault_sequence(history)
      bug_database.add(minimal_sequence)
      
      # Replay to confirm
      for 100 iterations:
        replay_exact_sequence(minimal_sequence)
        if bug_not_reproduced:
          # Heisenbug - timing dependent
          increase_pause_durations()
      
      # Explore neighborhood
      for variant in generate_variants(minimal_sequence):
        run_test(variant)
    
    Else:
      # No bug found
      if iterations > 1000:
        # Increase fault intensity
        increase_fault_probability()
        increase_pause_durations()
```

---

## Part 13: Conceptual Differences: Jepsen vs. Filesystem Testing

### What Jepsen Has That We Don't Need

**1. Network simulation**:
- Jepsen: iptables rules, network namespaces
- Us: Not applicable (single-node)

**2. Distributed consensus**:
- Jepsen: Tests Paxos, Raft
- Us: Not applicable

**3. Eventual consistency**:
- Jepsen: Tests CRDTs, gossip
- Us: Directory is immediately consistent (within lock)

### What We Have That Jepsen Doesn't

**1. Kernel-level fault injection**:
- Jepsen: Userspace only
- Us: eBPF can hook kernel internals

**2. Exact timing control**:
- Jepsen: Network delays (ms-second granularity)
- Us: eBPF can pause at microsecond granularity

**3. Memory/pointer manipulation**:
- Jepsen: Can't access other processes' memory
- Us: eBPF can read kernel memory (carefully)

**4. Lower-level hooks**:
- Jepsen: Application-level
- Us: VFS, filesystem, block layer, scheduler

---

## Part 14: Conceptual eBPF Nemesis Modes

### Mode 1: Probabilistic (Random)

**Concept**: Every hook has probability of triggering

```
Configuration:
  global_fault_rate: 10%  # Base probability
  
  per_hook_multiplier:
    vfs_getdents_async: 1.0  # 10% * 1.0 = 10%
    ext4_iterate_async: 2.0  # 10% * 2.0 = 20%
    kmalloc: 0.5             # 10% * 0.5 = 5%

At each hook:
  rand = random()
  threshold = global_rate * hook_multiplier
  if rand < threshold:
    inject_fault()
```

**Pros**: Explores wide range of scenarios
**Cons**: No control over specific patterns

---

### Mode 2: Deterministic (Scripted)

**Concept**: Follow a specific fault script

```
FaultScript:
  events = [
    {time: 5s, action: pause(thread_0, 5ms), location: vfs_entry},
    {time: 10s, action: kill(thread_1)},
    {time: 15s, action: pause(thread_2, 10ms), location: ext4_bread},
    {time: 20s, action: inject_enomem()},
  ]

Scheduler:
  For each event in script:
    wait_until(event.time)
    execute(event.action)
```

**Pros**: Reproducible, can replay exact bug scenarios
**Cons**: Doesn't explore unexpected combinations

---

### Mode 3: Adaptive (Learning)

**Concept**: Learn from history, focus on promising areas

```
AdaptiveNemesis:
  knowledge_base = {
    "vfs_entry pause": {triggers: 0, bugs_found: 0},
    "ext4_bread pause": {triggers: 100, bugs_found: 2},  # HIGH!
    "kmalloc fail": {triggers: 50, bugs_found: 0},
  }
  
  Choose next fault:
    # Prioritize hooks that have found bugs
    score(hook) = bugs_found / triggers
    
    # But also explore untried combinations
    score += exploration_bonus(hook)
    
    # Choose probabilistically
    hook = weighted_random_choice(scores)
    
    inject_fault_at(hook)
    knowledge_base[hook].triggers++
    
  If bug found at hook:
    knowledge_base[hook].bugs_found++
    # Future: Increase probability of testing this hook
```

**Pros**: Efficiently finds bugs, focuses on weak areas
**Cons**: Complex to implement

---

### Mode 4: Adversarial (Worst-Case)

**Concept**: Actively try to break specific invariants

```
AdversarialNemesis:
  target_invariant = "no_duplicate_entries"
  
  Strategy to break it:
    1. Wait for Thread A to read entry "file_X"
    2. IMMEDIATELY pause Thread A
    3. Cause Thread A's f_pos to be corrupted (if possible)
    4. Resume Thread A
    5. Check if Thread A reads "file_X" again
  
  Implementation:
    eBPF hooks track state:
      - Which entries each thread has seen
      - Current offset for each thread
    
    When Thread A sees "file_X":
      trigger_fpos_corruption_attempt(Thread A)
      
    Check results:
      if Thread A sees "file_X" again:
        BUG FOUND!
```

**This is powerful**: Directly targets suspected weak points

---

## Part 15: Conceptual Multi-Layer Fault Injection

### Jepsen Insight: Layer Violations

**Jepsen tests**: "What if network acts differently than application assumes?"

**Our analog**: "What if VFS assumptions about filesystems are violated?"

**Conceptual multi-layer faults**:

```
Layer 1: VFS Layer
  Assumptions:
    - Filesystem iterate_async doesn't block (if NOWAIT)
    - next_offset is always valid
    - Filesystem frees fs_private on error
  
  Faults to inject:
    - Make filesystem block even with NOWAIT
    - Return invalid next_offset
    - Leak fs_private (don't call cleanup)

Layer 2: Filesystem Layer
  Assumptions:
    - Blocks in cache don't disappear
    - Inodes stay valid during operation
    - Cursor encoding stable
  
  Faults to inject:
    - Evict block from cache mid-operation
    - Invalidate inode
    - Corrupt cursor

Layer 3: Block Layer
  Assumptions:
    - Reads don't fail randomly
    - Data integrity
    - Reasonable latency
  
  Faults to inject:
    - Random EIO
    - Corrupt data
    - Extreme latency (100ms+)

Test: Does each layer handle lower-layer violations gracefully?
```

**Conceptual test**:
```
MultiLayerFaultTest:
  # Inject faults at ALL layers simultaneously
  
  VFS_nemesis.config:
    force_block_on_nowait: 2%
    return_invalid_offset: 1%
  
  FS_nemesis.config:
    evict_blocks: 5%
    corrupt_cursor: 1%
  
  Block_nemesis.config:
    random_eio: 3%
    delay_io: 10%
  
  Run for 60s with all active
  
  Expected: System handles gracefully, no crashes
  Reality: Will likely find assumptions that are violated!
```

---

## Part 16: Conceptual eBPF Event Correlation

### Beyond Single Events

**Jepsen correlates events** across nodes to find bugs

**Example**:
```
Node A writes X=1
Node B reads X=0
Node C reads X=1

Question: Is this linearizable?
Answer: Only if we can order operations such that B's read happens-before A's write

Jepsen finds this by analyzing COMPLETE history
```

**Our analog**:

**Correlate events across threads**:
```
Thread 1: getdents → saw [a, b, c]      (time: 100ms)
Thread 2: create(d)                     (time: 50ms)
Thread 3: getdents → saw [a, b, c]      (time: 150ms)

Question: Should Thread 3 see 'd'?
Analysis:
  - create(d) completed at 50ms
  - Thread 3 started at 150ms (100ms after)
  - Thread 3 SHOULD see 'd'
  - If it doesn't: ANOMALY!
```

**Conceptual eBPF implementation**:
```
eBPF tracks:
  - When each file created/deleted (from create/unlink hooks)
  - When each getdents started/completed
  - What each getdents saw

BPF Map: directory_state_log
  Key: {inode, timestamp}
  Value: {operation_type, filename, thread_id}

At each getdents completion:
  record_snapshot(thread_id, entries_seen, time)

Post-processing (userspace):
  For each getdents operation:
    expected_files = compute_expected_files(operation.start_time)
    actual_files = operation.entries_seen
    
    missing = expected_files - actual_files
    extra = actual_files - expected_files
    
    if missing and file_created_before_scan:
      ANOMALY: Should have seen this file
    
    if extra and file_never_existed:
      BUG: Saw non-existent file!
```

---

## Part 17: Conceptual State Space Exploration

### Jepsen's Approach: Explore State Space Systematically

**Idea**: Don't just run random operations, explore state space

**For databases**:
```
State space:
  - Number of nodes: 1-5
  - Network topology: fully connected, partitioned, isolated
  - Node states: leader, follower, candidate
  - Operations: read, write, CAS

Explore:
  - All combinations
  - Focus on "interesting" states (partitioned leader)
  - Generate operations that exercise state transitions
```

**For filesystems**:
```
State space:
  - Directory size: empty, small (10), medium (1K), large (100K)
  - Directory format: linear, htree, B-tree
  - Concurrent readers: 0, 1, 10, 100
  - Concurrent writers: 0, 1, 10
  - Cache state: hot, cold, partial
  - Lock state: free, shared, contended

Explore systematically:
  Test 1: empty + 1 reader + cold cache
  Test 2: small + 10 readers + hot cache
  Test 3: medium + 10 readers + 10 writers + cold cache
  ...
  
  For each state:
    - Run operations
    - Inject faults appropriate to state
    - Check invariants
```

**Conceptual test matrix**:
```
State: {size=large, readers=10, writers=5, cache=cold}

Faults to inject (targeted):
  - Pause during cache misses (frequent in cold state)
  - Inject EAGAIN on lock (contended with 10 readers)
  - Kill writers (tests partial modification)

Expected outcome:
  - High EAGAIN rate (cold cache + contention)
  - Some missing entries (writers killed mid-operation)
  - No duplicates
  - No crashes
```

---

## Part 18: The Grand Vision: Filesystem Jepsen

### Hypothetical Complete System

```
┌─────────────────────────────────────────────────────────┐
│                  Filesystem Jepsen                      │
│                                                         │
│  ┌──────────────────────────────────────────────────┐  │
│  │  1. Workload Generator                           │  │
│  │     - Creates operations (read dir, create file) │  │
│  │     - Multiple concurrent clients                │  │
│  │     - Records invocations                        │  │
│  └──────────────────────────────────────────────────┘  │
│                          │                              │
│  ┌──────────────────────┴───────────────────────────┐  │
│  │  2. Nemesis (Fault Injector)                     │  │
│  │                                                   │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌──────────┐ │  │
│  │  │   Pauser    │  │Fault Injector│  │Modifier  │ │  │
│  │  │  (eBPF +    │  │  (eBPF      │  │(userspace│ │  │
│  │  │   ptrace)   │  │   override)  │  │ threads) │ │  │
│  │  └─────────────┘  └─────────────┘  └──────────┘ │  │
│  │         │                │                │       │  │
│  │         └────────────────┴────────────────┘       │  │
│  │                      │                            │  │
│  └──────────────────────┼────────────────────────────┘  │
│                         │                               │
│  ┌──────────────────────┴────────────────────────────┐  │
│  │  3. History Recorder                              │  │
│  │     - Captures all operations                     │  │
│  │     - Captures all faults                         │  │
│  │     - High-resolution timestamps                  │  │
│  │     - CPU/thread/process IDs                      │  │
│  └───────────────────────────────────────────────────┘  │
│                         │                               │
│  ┌──────────────────────┴────────────────────────────┐  │
│  │  4. Checker                                       │  │
│  │     - Validates invariants                        │  │
│  │     - Detects anomalies                           │  │
│  │     - Checks consistency model                    │  │
│  └───────────────────────────────────────────────────┘  │
│                         │                               │
│  ┌──────────────────────┴────────────────────────────┐  │
│  │  5. Analyzer                                      │  │
│  │     - Visualizes timelines                        │  │
│  │     - Finds minimal reproducers                   │  │
│  │     - Generates reports                           │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

---

## Part 19: Specific Research Questions

### Question 1: What Pauses Matter Most?

**Research approach**:

**Hypothesis 1**: "Pausing between lock acquire and lock use triggers most races"

Test:
- Configure eBPF to ONLY pause at that point
- Run for 1 hour
- Count bugs found

**Hypothesis 2**: "Pausing during cursor decode triggers cursor bugs"

Test:
- Configure eBPF to ONLY pause at cursor operations
- Run for 1 hour
- Count bugs found

**Compare**: Which hypothesis found more bugs?

**Conceptual result**:
```
Hook Point                    Bugs Found    Unique Bugs
─────────────────────────────────────────────────────────
vfs_getdents:entry           12            8
after_inode_lock             25            15  ← MOST!
ext4_htree_lookup            8             6
ext4_bread                   18            10
before_copy_to_user          5             3
after_inode_unlock           3             2

Conclusion: Pausing after lock acquisition finds most bugs
Action: Increase probability at that hook point
```

---

### Question 2: What Fault Correlations Exist?

**Research approach**: Try all pairs of faults

```
Fault A: pause_at_lock
Fault B: inject_enomem

Test combinations:
  A alone: 5 bugs found
  B alone: 3 bugs found
  A + B together: 12 bugs found  ← MORE than sum!
  
This is a "synergistic pair" - test together more often
```

**Conceptual correlation matrix**:
```
              Pause  ENOMEM  EIO  Kill  Cache
Pause           -      +      ++    +     ++
ENOMEM          +      -      +     +     +
EIO            ++      +      -     +     ++
Kill            +      +      +     -     +
Cache Flush    ++      +     ++     +     -

Legend:
  -  : N/A (same fault)
  +  : Normal correlation (bugs = sum of individual)
  ++ : Synergistic (bugs > sum)
```

**Strategy**: Favor synergistic pairs (++ entries)

---

### Question 3: Do Bugs Cluster?

**Research approach**: Analyze bug discovery timeline

```
Time → | 0s    10s   20s   30s   40s   50s   60s
Bugs   | 0     5     12    15    16    16    16
       |       ↑     ↑
       |     Cluster (many bugs in 10-20s range)

Question: What was different at 10-20s?
Answer: That's when "aggressive phase" started (20% fault rate)

Insight: Need aggressive testing to find most bugs
Action: Start aggressive phase earlier
```

**Conceptual bug clustering**:
```
Bugs by category:
  f_pos corruption: 8 bugs
  cursor invalidation: 5 bugs
  lock deadlock: 2 bugs
  memory leak: 1 bug

Bugs by fault type that triggered them:
  pause_after_lock: 10 bugs  ← Clusters here!
  enomem: 4 bugs
  eio: 2 bugs

Insight: pause_after_lock is the most effective fault
Action: Use it 50% of the time
```

---

## Part 20: Conceptual Minimization Strategy

### Jepsen's Minimizer

**When bug found**, Jepsen minimizes:
1. Number of operations
2. Number of faults
3. Fault duration

**Goal**: Find minimal reproducing case

**Example from Jepsen**:
```
Original failing history: 100 operations, 50 faults
After minimization: 3 operations, 1 fault

Minimal case:
  Op 1: write(x=1)
  Fault: partition network
  Op 2: write(x=2) on Node A
  Op 3: read(x) on Node B → sees x=0 (BUG!)
```

**Our conceptual analog**:

**Original failing scenario**:
```
Operations: 1000 getdents, 500 creates, 500 deletes
Faults: 200 pauses, 50 ENOMEM, 30 EIO
Duration: 60 seconds
Bug: Duplicate entry seen by Thread 5 at T=35s
```

**Minimization process**:
```
Step 1: Remove operations that happened after bug
  Result: 600 ops (up to T=35s)
  Bug still reproduces? YES → keep
  
Step 2: Remove operations from unrelated threads
  Result: Thread 5 ops only (50 ops)
  Bug still reproduces? NO → add back Thread 2 ops
  Result: Thread 2 + Thread 5 ops (120 ops)
  Bug still reproduces? YES → keep
  
Step 3: Remove faults that didn't affect these threads
  Result: 15 pauses (on Thread 2 and 5)
  Bug still reproduces? YES → keep
  
Step 4: Binary search on pause durations
  Try: 0.5ms instead of 5ms
  Bug still reproduces? NO → need at least 2ms
  Result: 2ms minimum pause duration
  
Step 5: Remove pauses one-by-one
  Result: Pause at T=34.5s in vfs_getdents_async is essential
  All other pauses can be removed
  
Minimal reproducer:
  Thread 2: create(file_42) at T=34.0s
  Thread 5: getdents(offset=0x1000) at T=34.2s
  Nemesis: pause(Thread 5, 2ms) at vfs_entry, T=34.5s
  Result: Thread 5 sees file_42 twice
  
Root cause: f_pos corruption when Thread 5 paused
```

---

## Part 21: Conceptual Visualization & Analysis

### Jepsen's Analysis Tools

**Jepsen generates**:
- Timeline diagrams
- Anomaly reports
- Linearizability graphs

**Our conceptual analog**:

#### Visualization 1: Thread Timeline

```
Thread 0: ──[getdents start]───────[pause 5ms]───────[getdents end]──
                                       ↓
Thread 1: ──────[create(X)]────[ok]───────[delete(Y)]────[ok]────
                                       ↓
Thread 2: ──────────[getdents start]──────────────[getdents end]────
                                       ↓
Nemesis:  ────────────────────[PAUSE THREAD 0]────────────────────

Analysis:
  Thread 0 started before create(X)
  Thread 0 paused during create(X)
  Thread 0 resumed after create(X)
  Question: Did Thread 0 see X?
  Expected: Maybe (POSIX allows either)
  Actual: Saw X twice (BUG!)
```

#### Visualization 2: Offset Tracking

```
Thread A's offset progression:
Time: 0ms    10ms   15ms   20ms   25ms   30ms
      0 ───→ 0x100 → PAUSE → 0x200 → 0x300 → EOF
                       ↓
                     5ms delay
                       ↓
                    During pause:
                      Thread B modified directory
                      Offset 0x200 now invalid

Result: Thread A got -EINVAL at 20ms
Diagnosis: Cursor invalidated during pause
```

#### Visualization 3: Fault Density Heat Map

```
Kernel Function         Faults Injected    Bugs Found
─────────────────────────────────────────────────────
vfs_getdents_async      ████████ (800)     ████ (40)
ext4_iterate_async      ████████ (750)     ████████ (80) ← HOT!
ext4_bread              ████ (400)         ██ (20)
down_read               ██████ (600)       ████ (45)

Insight: ext4_iterate_async is a bug hotspot
Action: Focus testing there
```

---

## Part 22: Conceptual Property-Based Testing

### Jepsen Uses Generators

**Concept**: Generate random but valid operation sequences

**Example**:
```clojure
(gen/mix [
  (gen/read :x)
  (gen/write :x {:value (gen/int)})
  (gen/cas :x {:from (gen/int) :to (gen/int)})
])
```

**Our conceptual generator**:

```
OperationGenerator:
  operations = [
    {:type: getdents, :weight: 0.7},
    {:type: create, :weight: 0.15},
    {:type: delete, :weight: 0.10},
    {:type: rename, :weight: 0.05},
  ]
  
  For each thread:
    While not_stopped:
      op = weighted_random_choice(operations)
      
      if op.type == getdents:
        offset = thread.last_offset OR 0
        result = getdents(offset)
        record_history("getdents", offset, result, time)
        thread.last_offset = result.next_offset
      
      if op.type == create:
        name = generate_unique_name(thread.id)
        create(directory / name)
        record_history("create", name, time)
      
      pause_random(0-10ms)  // Variable think time
```

**Properties to test**:
```
Property 1: "Eventually see all files"
  If no creates/deletes after T, and keep reading
  Eventually: seen_entries == all_files

Property 2: "Cursor makes progress"
  next_offset != current_offset (unless EOF)

Property 3: "EOF is stable"
  If reached EOF, all future reads return EOF

Property 4: "Entries are valid"
  All entries have valid inodes, types, names
```

---

## Part 23: Conceptual Open Questions for Research

### Question 1: Can We Use Formal Methods?

**Idea**: Model directory iteration in TLA+ or similar

**Conceptual TLA+ model**:
```
Variables:
  directory_contents: Set of filenames
  thread_offsets: Map from ThreadID to Offset
  locks_held: Set of ThreadIDs

Invariants:
  No two threads hold exclusive lock simultaneously
  If thread sees entry E, E existed at some point during scan
  
Actions:
  getdents(thread, offset):
    IF lock_available:
      acquire_lock(thread)
      entries = snapshot(directory_contents, offset)
      release_lock(thread)
      return entries
```

**Benefit**: Can formally verify properties

**Challenge**: State space explosion with many threads

---

### Question 2: Can We Detect Bugs Automatically?

**Idea**: Use ML to detect "anomalous" patterns in history

**Conceptual approach**:
```
Train model on:
  - Histories from successful tests (no bugs)
  - Features: operation counts, timing, patterns

At test time:
  - Extract features from history
  - Compare to learned normal behavior
  - If deviation > threshold: Possible bug

Example:
  Normal: getdents takes 10-100us
  Anomaly: getdents took 10s (possible deadlock)
  
  Normal: EAGAIN rate 5-10%
  Anomaly: EAGAIN rate 50% (possible livelock)
```

---

### Question 3: How to Handle Non-Determinism?

**Challenge**: Same test produces different results

**Jepsen's approach**: Run same test 100s of times, look for ANY failure

**Conceptual strategy**:
```
DeterminismAnalysis:
  test = "concurrent_readers_10_threads"
  
  results = []
  for i in 1..1000:
    result = run_test()
    results.append(result)
  
  Analyze:
    failure_rate = count(results, "FAIL") / 1000
    
    if failure_rate == 0:
      PASS: Test is solid
    
    if failure_rate < 0.01:
      FLAKY: Rare bug (heisenbug)
      Action: Increase fault injection to make reproducible
    
    if failure_rate > 0.01:
      REAL BUG: Consistently reproducible
      Action: Minimize and fix
```

---

## Part 24: Research Synthesis

### Key Insights from Jepsen Applied to Filesystems

**1. Concurrent operations + faults = bug finder**
- Don't just test operations in isolation
- Don't just inject faults alone
- Combine them: operations DURING faults

**2. History is essential**
- Can't debug without complete history
- Need to reconstruct "what happened"
- Post-mortem analysis is where bugs are understood

**3. Phases matter**
- Don't inject maximum chaos immediately
- Ramp up: gentle → aggressive → extreme
- Allows finding both common and rare bugs

**4. Minimization is key**
- Complex failing scenarios are hard to understand
- Minimize to essence
- "3 operations + 1 fault" is easier to fix than "1000 ops + 200 faults"

**5. Systematic exploration beats random**
- Don't just run random operations
- Explore state space systematically
- Focus on "interesting" states (contended locks, cold cache, etc.)

---

## Part 25: Conceptual Fault Injection Patterns

### Pattern 1: The Timing Sandwich

**Concept**: Pause before AND after critical operation

```
Pattern:
  1. Pause Thread A (5ms)
  2. Let Thread B execute
  3. Resume Thread A
  4. IMMEDIATELY pause Thread A again (5ms)
  5. Let Thread C execute
  
Effect: Thread A sees inconsistent state from B and C
```

### Pattern 2: The Resource Starvation

**Concept**: Fail all allocations for brief period

```
Pattern:
  1. Normal operation (T=0-10s)
  2. Fail ALL kmalloc for 100ms (T=10.0-10.1s)
  3. Resume normal (T=10.1s+)
  
Effect: Burst of ENOMEM errors
Tests: Recovery from transient resource exhaustion
```

### Pattern 3: The Cascading Failure

**Concept**: One failure triggers another

```
Pattern:
  1. Inject EIO on block read
  2. Thread returns -EIO to user
  3. User retries
  4. Inject ENOMEM on retry
  5. Thread returns -ENOMEM
  6. User retries again
  7. Inject pause on third retry
  
Effect: Tests multi-fault recovery
```

### Pattern 4: The Thundering Herd

**Concept**: Many threads wake up simultaneously

```
Pattern:
  1. Pause all reader threads (10-50 threads)
  2. Hold them for 5s
  3. Resume ALL at exact same time
  4. All try to acquire lock simultaneously
  
Effect: Extreme lock contention
Tests: Fairness, starvation, livelock
```

---

## Part 26: Conceptual Next Steps for Research

### Experiments to Run (Conceptual)

**Experiment 1: Fault Effectiveness Study**

Goal: Which faults find the most bugs?

Method:
  1. Implement all fault types
  2. Run each type in isolation (1000 iterations)
  3. Count unique bugs found by each
  4. Rank by effectiveness

**Experiment 2: Pause Duration Study**

Goal: Optimal pause duration?

Method:
  1. Run test with pause durations: 10us, 100us, 1ms, 10ms, 100ms
  2. Measure: bugs found vs test time
  3. Find sweet spot (most bugs per minute)

**Experiment 3: Correlation Discovery**

Goal: Which fault combinations are synergistic?

Method:
  1. Test all pairs of faults
  2. Compare: bugs(A+B) vs bugs(A) + bugs(B)
  3. Identify synergistic pairs
  4. Build fault sequences from synergistic pairs

**Experiment 4: Hook Point Importance**

Goal: Which hook points matter most?

Method:
  1. For each hook point, enable ONLY that hook
  2. Run for 1 hour
  3. Count bugs found
  4. Rank hook points by bug yield
  5. Focus future testing on high-yield hooks

---

## Summary

### Jepsen Principles Mapped to Filesystem Testing

| Jepsen Principle | Filesystem Application |
|------------------|------------------------|
| **Concurrent clients** | Concurrent reader/writer threads |
| **Nemesis (faults)** | eBPF pause/fault injection |
| **History recording** | Complete operation log with timestamps |
| **Checker (invariants)** | No duplicates, no missing, consistency |
| **Linearizability** | Weaker model (snapshot consistency) |
| **Minimization** | Find minimal reproducer |
| **Visualization** | Thread timelines, fault heat maps |
| **Property-based** | Generate random valid operation sequences |
| **Adaptive** | Learn from bugs, focus on weak areas |

---

### Research Areas to Explore

**1. Fault Taxonomy**: What faults exist in filesystem domain?
- ✅ Covered in this document

**2. Hook Point Strategy**: Where to inject for maximum effect?
- ✅ Conceptual design provided
- ⏳ Need empirical validation

**3. Correlation Analysis**: Which fault combinations are synergistic?
- ✅ Conceptual approach defined
- ⏳ Need to implement and measure

**4. History Model**: How to record and analyze operations?
- ✅ JSON format proposed
- ⏳ Need to implement recorder

**5. Checker Design**: How to validate filesystem-specific invariants?
- ✅ 4 checkers designed
- ⏳ Need to implement

**6. Minimization**: How to find minimal reproducers?
- ✅ Algorithm outlined
- ⏳ Need to implement

---

### Conceptual Architecture Summary

```
The Complete Filesystem Jepsen:

[Operation Generator] ──┐
                        │
[Modifier Threads]   ───┼──→ [Operations] ──→ [Filesystem]
                        │                           ↑
[Reader Threads]     ───┘                           │
                                                    │
[eBPF Nemesis]      ────────→ [Faults] ────────────┘
  - Pauser                       - Pauses
  - Fault Injector               - Errors
  - State Corruptor              - Invalid states
                                                    ↓
[History Recorder]  ←──────────────────────[All Events]
                                                    ↓
[Checker]           ←──────────────────────[History]
  - No duplicates                               ↓
  - Snapshot consistency                    [Report]
  - Progress                                    ↓
  - Cursor validity                       [Visualization]
                                                ↓
[Minimizer]         ←──────────────────────[If Bug Found]
                                                ↓
                                          [Minimal Reproducer]
```

**All components conceptually designed**

**Next step**: Implement piece by piece, validate empirically

---

*This document provides a conceptual blueprint for Jepsen-quality filesystem testing using eBPF.*

*No code yet - pure design and research.*

*Use this as a foundation for implementing the chaos testing framework.*

