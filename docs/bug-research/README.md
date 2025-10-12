# Bug Research Documentation

This directory contains research on real-world Linux filesystem corruption bugs to validate Xibalba's bug detection capabilities.

## Purpose

These documents catalog actual bugs that evaded detection for extended periods (sometimes years), providing concrete targets for testing whether Xibalba's fault injection and testing framework could have detected them earlier.

## Documents in This Directory

### 1. [FILESYSTEM-CORRUPTION-BUGS-CATALOG.md](FILESYSTEM-CORRUPTION-BUGS-CATALOG.md)
**Comprehensive catalog of 16+ real filesystem bugs (2018-2025)**

- **What:** Complete bug database with technical details, CVE numbers, and analysis
- **Contents:** 
  - Executive summary of findings
  - Detailed bug descriptions with root causes
  - Links to LKML, CVEs, bug trackers, LWN articles
  - Quick reference table
  - Categorization by bug type
- **Use for:** Understanding what types of bugs exist and their technical details

**Key Finding:** Many bugs went undetected for years. Some are perfect targets for Xibalba's pause injection and crash simulation.

### 2. [PRIORITY-BUGS-FOR-TESTING.md](PRIORITY-BUGS-FOR-TESTING.md)
**8 high-priority bugs with test implementation plans**

- **What:** Focused list of bugs Xibalba should test against
- **Contents:**
  - Detailed test scenarios with code examples
  - Links to all bug reports and discussions
  - Implementation strategy (Phase 1, 2, 3)
  - Success criteria
  - CrashMonkey comparison
  - 70+ direct links to primary sources
- **Use for:** Planning test implementation; understanding how to reproduce bugs

**Categories:**
- 3 race condition bugs (OCFS2, OpenZFS, ext4 iomap)
- 3 crash consistency bugs (ext4 delayed alloc, ReiserFS rename, Btrfs RAID)
- 2 metadata consistency bugs (F2FS GC, XFS)

### 3. [XIBALBA-CURRENT-CAPABILITIES-VS-BUGS.md](XIBALBA-CURRENT-CAPABILITIES-VS-BUGS.md)
**Gap analysis: What can Xibalba detect NOW vs what's needed**

- **What:** Analysis of current capabilities and implementation roadmap
- **Contents:**
  - Current Xibalba capabilities inventory
  - Gap analysis for each priority bug
  - Effort estimates for each bug
  - Detection confidence ratings
  - Implementation recommendations with code samples
- **Use for:** Deciding what to implement next; understanding effort required

**Key Recommendation:** Start with ext4 iomap Direct I/O bug (2-3 hours, uses existing pause injection)

## Document Flow

```
1. CATALOG: What bugs exist?
      ↓
2. PRIORITY: Which should we test?
      ↓
3. CAPABILITIES: Can we test them? How?
```

## Quick Start

**If you want to implement a bug test:**

1. Read `PRIORITY-BUGS-FOR-TESTING.md` to pick a bug
2. Check `XIBALBA-CURRENT-CAPABILITIES-VS-BUGS.md` for gap analysis
3. Reference `FILESYSTEM-CORRUPTION-BUGS-CATALOG.md` for technical details
4. Follow the implementation code samples provided

**Recommended starting point:** ext4 iomap Direct I/O bug (Section in CAPABILITIES doc)

## Key Findings Summary

### Bug Categories Found:
- **Race Conditions:** 6 bugs (pause injection is perfect for these)
- **Crash Consistency:** 4 bugs (need crash simulation)
- **Metadata Issues:** 5 bugs (complex state tracking)
- **Timing/Ordering:** 3 bugs (pause injection helps)

### Xibalba's Sweet Spot:
1. ✅ **Race condition bugs** - Pause injection widens race windows
2. ✅ **Crash consistency** - Need to add crash simulation
3. ✅ **Timing-dependent bugs** - Already have pause injection

### Validation:
CrashMonkey (academic tool with similar approach) found **10 NEW bugs** using bounded crash testing with small workloads. This validates Xibalba's methodology.

## Primary Sources Referenced

**Official Sources:**
- 9 LKML discussions (lkml.org)
- 6 CVE database entries
- 10+ bug tracker reports (Debian, Ubuntu, Kernel Bugzilla)

**Technical Analysis:**
- 6 LWN.net articles (highly recommended reading)
- CrashMonkey OSDI 2018 paper

**Community:**
- Forum discussions (Arch, Phoronix, Hacker News)
- Wikipedia articles on filesystem behaviors

**Total:** 70+ direct links across all documents

## Navigation

**From main docs:**
- Architecture: [`docs/design/TESTING-FRAMEWORK.md`](../design/TESTING-FRAMEWORK.md)
- eBPF design: [`docs/design/EBPF-FAULT-INJECTION-FOR-POSIX-BUGS.md`](../design/EBPF-FAULT-INJECTION-FOR-POSIX-BUGS.md)
- Quick start: [`docs/guides/QUICK-START.md`](../guides/QUICK-START.md)

**Related directories:**
- [`docs/design/`](../design/) - Technical design documents
- [`docs/plans/`](../plans/) - Implementation plans
- [`docs/status/`](../status/) - Current status updates

## Research Methodology

This research was conducted by:
1. Web searches for filesystem corruption bugs (2018-2025)
2. Analysis of LKML archives and CVE databases
3. Review of LWN.net technical articles
4. Academic paper review (CrashMonkey, ACE)
5. Community forum analysis

All bugs are real, documented issues with public references.

## Contributing

When adding new bugs to this research:

1. Add to `FILESYSTEM-CORRUPTION-BUGS-CATALOG.md` with full details
2. Evaluate for `PRIORITY-BUGS-FOR-TESTING.md` (is it testable?)
3. Update `XIBALBA-CURRENT-CAPABILITIES-VS-BUGS.md` gap analysis
4. Include links to primary sources (LKML, CVE, bug trackers)

---

*Last Updated: October 12, 2025*  
*Research compiled for Xibalba Filesystem Testing Framework*

