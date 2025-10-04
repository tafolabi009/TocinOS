# TocinOS Advanced Features - Implementation Guide

This guide provides practical, step-by-step instructions for implementing the advanced features outlined in the roadmap. It's designed for contributors who want to help build TocinOS into a world-class operating system.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Phase 1: KASLR Implementation](#phase-1-kaslr-implementation)
3. [Phase 2: CFS Scheduler](#phase-2-cfs-scheduler)
4. [Phase 3: TocinFS Filesystem](#phase-3-tocinfs-filesystem)
5. [Phase 4: Security Framework](#phase-4-security-framework)
6. [Phase 5: Graphics Stack](#phase-5-graphics-stack)
7. [Testing Guidelines](#testing-guidelines)
8. [Performance Benchmarking](#performance-benchmarking)
9. [Documentation Standards](#documentation-standards)
10. [Code Review Checklist](#code-review-checklist)

---

## Getting Started

### Prerequisites

Before implementing advanced features, ensure you have:

1. **Working TocinOS build**: Can build and run in QEMU
2. **Development tools**: GCC, NASM, Make, QEMU
3. **Understanding of existing code**: Read ARCHITECTURE.md and FEATURES.md
4. **Test environment**: Virtual machines for testing
5. **Version control**: Git repository configured

### Development Workflow

```bash
# 1. Create feature branch
git checkout -b feature/kaslr-implementation

# 2. Make changes iteratively
# ... edit files ...

# 3. Test frequently
make clean && make ARCH=x86_64
make run64

# 4. Commit with descriptive messages
git add -A
git commit -m "kernel: Add KASLR entropy gathering"

# 5. Push and create pull request
git push origin feature/kaslr-implementation
```

### Code Style

TocinOS follows these coding standards:

- **C code**: Linux kernel coding style
- **Assembly**: NASM syntax with clear comments
- **Indentation**: 4 spaces (not tabs)
- **Line length**: Max 100 characters
- **Comments**: Required for complex logic
- **Function headers**: Document parameters and return values

---

## Phase 1: KASLR Implementation

### Step 1.1: Create KASLR Header

```bash
# Create the header file
touch include/kernel/kaslr.h
```

```c
// include/kernel/kaslr.h
#ifndef KERNEL_KASLR_H
#define KERNEL_KASLR_H

#include <stdint.h>

// KASLR configuration
#define KASLR_ENTROPY_BITS      10              // 1024 positions
#define KASLR_ALIGNMENT         0x200000        // 2MB alignment

// 64-bit kernel address space
#ifdef __x86_64__
#define KERNEL_BASE_MIN         0xFFFFFFFF80000000ULL
#define KERNEL_BASE_MAX         0xFFFFFFFFC0000000ULL
#else
// 32-bit: Limited to 1GB window
#define KERNEL_BASE_MIN         0xC0000000
#define KERNEL_BASE_MAX         0xD0000000
#endif

// KASLR state
typedef struct {
    uint64_t kernel_base;       // Randomized base address
    uint64_t kernel_size;       // Kernel size in bytes
    uint64_t random_offset;     // Offset applied
    uint32_t entropy;           // Entropy bits used
    int enabled;                // KASLR enabled flag
} kaslr_state_t;

// Functions
void kaslr_init(void);
uint64_t kaslr_get_base(void);
int kaslr_is_enabled(void);
void kaslr_relocate_kernel(void);

#endif // KERNEL_KASLR_H
```

### Step 1.2: Implement Entropy Gathering

```bash
# Create implementation file
touch kernel/kaslr.c
```

```c
// kernel/kaslr.c
#include "../include/kernel/kaslr.h"
#include "../include/kernel/cpu_info.h"
#include "../include/kernel/kernel.h"

static kaslr_state_t kaslr_state = {0};

/**
 * Check if RDRAND is available
 */
static inline int has_rdrand(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (ecx & (1 << 30)) != 0;  // RDRAND bit
}

/**
 * Get random value using RDRAND
 */
static uint64_t rdrand64(void) {
    uint64_t val;
    int retries = 10;
    
    while (retries-- > 0) {
        asm volatile(
            "rdrand %0\n\t"
            "jc 1f\n\t"
            "xor %0, %0\n\t"
            "1:\n\t"
            : "=r" (val)
            :
            : "cc"
        );
        
        if (val != 0)
            return val;
    }
    
    return 0;
}

/**
 * Get time stamp counter (for entropy)
 */
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/**
 * Gather entropy from multiple sources
 */
static uint64_t gather_entropy(void) {
    uint64_t entropy = 0;
    
    // Source 1: RDRAND (best option)
    if (has_rdrand()) {
        entropy = rdrand64();
    }
    
    // Source 2: TSC jitter
    for (int i = 0; i < 8; i++) {
        entropy ^= rdtsc();
        // Small delay
        for (volatile int j = 0; j < 100; j++);
    }
    
    // Source 3: Mix in memory address (ASLR of stack)
    uint64_t stack_addr = (uint64_t)&entropy;
    entropy ^= stack_addr;
    
    // Source 4: Mix in CPU features
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    entropy ^= ((uint64_t)eax << 32) | ecx;
    
    return entropy;
}

/**
 * Calculate randomized kernel base address
 */
static uint64_t calculate_kaslr_base(void) {
    uint64_t entropy = gather_entropy();
    
    // Extract KASLR_ENTROPY_BITS bits
    uint64_t offset = entropy & ((1ULL << KASLR_ENTROPY_BITS) - 1);
    
    // Multiply by alignment (2MB)
    offset *= KASLR_ALIGNMENT;
    
    // Add to minimum base
    uint64_t base = KERNEL_BASE_MIN + offset;
    
    // Ensure it doesn't exceed maximum
    if (base > KERNEL_BASE_MAX - kaslr_state.kernel_size) {
        base = KERNEL_BASE_MIN;  // Fallback to minimum
    }
    
    return base;
}

/**
 * Initialize KASLR
 * Called early in boot process
 */
void kaslr_init(void) {
    // Check if KASLR should be disabled (e.g., nokaslr boot parameter)
    // For now, always enable if supported
    
    #ifdef __x86_64__
    kaslr_state.enabled = 1;
    #else
    // KASLR less useful on 32-bit due to limited address space
    kaslr_state.enabled = 0;
    #endif
    
    if (!kaslr_state.enabled) {
        kaslr_state.kernel_base = KERNEL_BASE_MIN;
        kaslr_state.random_offset = 0;
        return;
    }
    
    // Calculate randomized base
    kaslr_state.kernel_base = calculate_kaslr_base();
    kaslr_state.random_offset = kaslr_state.kernel_base - KERNEL_BASE_MIN;
    
    kernel_print("[KASLR] Kernel base: 0x");
    print_hex64(kaslr_state.kernel_base);
    kernel_print("\n");
    
    kernel_print("[KASLR] Random offset: 0x");
    print_hex64(kaslr_state.random_offset);
    kernel_print("\n");
}

/**
 * Get kernel base address
 */
uint64_t kaslr_get_base(void) {
    return kaslr_state.kernel_base;
}

/**
 * Check if KASLR is enabled
 */
int kaslr_is_enabled(void) {
    return kaslr_state.enabled;
}

/**
 * Helper: Print 64-bit hex value
 */
static void print_hex64(uint64_t val) {
    char hex[] = "0123456789ABCDEF";
    char buf[17];
    buf[16] = '\0';
    
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    
    kernel_print(buf);
}
```

### Step 1.3: Integrate with Bootloader

```assembly
; boot/stage2/kaslr_setup.asm
; Called before loading kernel

setup_kaslr:
    push rbp
    mov rbp, rsp
    
    ; Check if KASLR should be enabled
    ; (Check boot parameters for "nokaslr")
    call check_boot_params
    test rax, rax
    jz .no_kaslr
    
    ; Gather entropy
    call gather_boot_entropy
    mov r12, rax                ; Save entropy
    
    ; Calculate random offset
    and r12, 0x3FF              ; 10 bits
    shl r12, 21                 ; * 2MB
    
    ; Add to base kernel address
    mov r13, 0xFFFFFFFF80000000 ; Kernel base
    add r13, r12                ; Add random offset
    
    ; Store for kernel to use
    mov [kaslr_base], r13
    mov [kaslr_offset], r12
    
    ; Print message
    mov rsi, kaslr_enabled_msg
    call print_string
    
    jmp .done
    
.no_kaslr:
    mov r13, 0xFFFFFFFF80000000 ; Default base
    mov [kaslr_base], r13
    xor r12, r12
    mov [kaslr_offset], r12
    
.done:
    pop rbp
    ret

gather_boot_entropy:
    ; Try RDRAND first
    mov ecx, 10                 ; Retries
.retry:
    rdrand rax
    jc .got_random
    loop .retry
    
    ; Fallback to RDTSC
    rdtsc
    shl rdx, 32
    or rax, rdx
    
.got_random:
    ret

kaslr_enabled_msg: db 'KASLR enabled', 0x0D, 0x0A, 0
kaslr_base: dq 0
kaslr_offset: dq 0
```

### Step 1.4: Update Makefile

```makefile
# Add KASLR object to kernel build
KERNEL_C_SOURCES += kernel/kaslr.c

# Add KASLR assembly to stage2 build (optional)
# BOOT_STAGE2_SOURCES += boot/stage2/kaslr_setup.asm
```

### Step 1.5: Test KASLR

```bash
# Build and run
make clean && make ARCH=x86_64
make run64

# Check kernel output for KASLR messages
# Should see:
# [KASLR] Kernel base: 0xFFFFFFFF8XXXXXXX
# [KASLR] Random offset: 0xXXXXXXXX

# Run multiple times and verify base changes
for i in {1..5}; do
    make run64 2>&1 | grep "KASLR"
done
```

---

## Phase 2: CFS Scheduler

### Step 2.1: Red-Black Tree Implementation

```bash
touch include/kernel/rbtree.h
touch kernel/rbtree.c
```

```c
// include/kernel/rbtree.h
#ifndef KERNEL_RBTREE_H
#define KERNEL_RBTREE_H

#include <stdint.h>

// Red-Black tree colors
#define RB_RED      0
#define RB_BLACK    1

// Red-Black tree node
typedef struct rb_node {
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;
    uint8_t color;
} rb_node_t;

// Red-Black tree root
typedef struct rb_root {
    struct rb_node *node;
} rb_root_t;

// Inline functions
static inline void rb_set_parent(rb_node_t *node, rb_node_t *parent) {
    node->parent = parent;
}

static inline void rb_set_color(rb_node_t *node, uint8_t color) {
    node->color = color;
}

// Functions
void rb_insert_color(rb_node_t *node, rb_root_t *root);
void rb_erase(rb_node_t *node, rb_root_t *root);
rb_node_t *rb_first(rb_root_t *root);
rb_node_t *rb_next(rb_node_t *node);
rb_node_t *rb_prev(rb_node_t *node);

// Helper macros
#define rb_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define rb_for_each_entry(pos, root, member) \
    for (pos = rb_entry(rb_first(root), typeof(*pos), member); \
         &pos->member; \
         pos = rb_entry(rb_next(&pos->member), typeof(*pos), member))

#endif // KERNEL_RBTREE_H
```

### Step 2.2: CFS Run Queue Structure

```c
// include/kernel/cfs.h
#ifndef KERNEL_CFS_H
#define KERNEL_CFS_H

#include "rbtree.h"
#include "task.h"

#define CFS_MIN_GRANULARITY  1000000   // 1ms in nanoseconds
#define CFS_TARGET_LATENCY   6000000   // 6ms target latency
#define NICE_0_LOAD          1024      // Default load weight

// Per-CPU CFS run queue
typedef struct cfs_runqueue {
    rb_root_t tasks_timeline;       // RB-tree of tasks
    rb_node_t *rb_leftmost;         // Cached leftmost node
    
    uint64_t min_vruntime;          // Minimum virtual runtime
    uint64_t load_weight;           // Total load weight
    uint32_t nr_running;            // Number of running tasks
    
    spinlock_t lock;                // Run queue lock
    
    uint64_t exec_clock;            // Execution clock
    uint64_t total_weight;          // Total task weight
} cfs_rq_t;

// CFS per-task data
typedef struct cfs_task_data {
    rb_node_t run_node;             // RB-tree node
    uint64_t vruntime;              // Virtual runtime
    uint64_t exec_start;            // Execution start time
    uint64_t sum_exec_runtime;      // Total execution time
    uint32_t load_weight;           // Task weight (from priority)
    int on_rq;                      // On run queue flag
} cfs_task_t;

// Priority to weight conversion (40 entries for nice -20 to +19)
extern const uint32_t prio_to_weight[40];
extern const uint32_t prio_to_wmult[40];

// Functions
void cfs_init(void);
void cfs_enqueue_task(task_t *task);
void cfs_dequeue_task(task_t *task);
task_t *cfs_pick_next_task(void);
void cfs_put_prev_task(task_t *task);
void cfs_task_tick(task_t *task);
uint64_t cfs_calculate_timeslice(task_t *task);

#endif // KERNEL_CFS_H
```

### Step 2.3: Implement CFS Core

```c
// kernel/cfs.c
#include "../include/kernel/cfs.h"
#include "../include/kernel/kernel.h"

// Priority to weight table
const uint32_t prio_to_weight[40] = {
    /* -20 */ 88761, 71755, 56483, 46273, 36291,
    /* -15 */ 29154, 23254, 18705, 14949, 11916,
    /* -10 */ 9548, 7620, 6100, 4904, 3906,
    /*  -5 */ 3121, 2501, 1991, 1586, 1277,
    /*   0 */ 1024, 820, 655, 526, 423,
    /*   5 */ 335, 272, 215, 172, 137,
    /*  10 */ 110, 87, 70, 56, 45,
    /*  15 */ 36, 29, 23, 18, 15,
};

static cfs_rq_t cfs_rq;

/**
 * Initialize CFS scheduler
 */
void cfs_init(void) {
    cfs_rq.tasks_timeline.node = NULL;
    cfs_rq.rb_leftmost = NULL;
    cfs_rq.min_vruntime = 0;
    cfs_rq.load_weight = 0;
    cfs_rq.nr_running = 0;
    spinlock_init(&cfs_rq.lock);
    
    kernel_print("[CFS] Completely Fair Scheduler initialized\n");
}

/**
 * Update current task's virtual runtime
 */
static void update_curr(task_t *task) {
    uint64_t now = timer_get_ticks();  // Get current time
    uint64_t delta_exec = now - task->cfs.exec_start;
    
    task->cfs.exec_start = now;
    task->cfs.sum_exec_runtime += delta_exec;
    
    // Calculate virtual runtime
    // vruntime_delta = delta_exec * NICE_0_LOAD / task_weight
    uint64_t vruntime_delta = (delta_exec * NICE_0_LOAD) / task->cfs.load_weight;
    task->cfs.vruntime += vruntime_delta;
    
    // Update run queue's min_vruntime
    if (cfs_rq.rb_leftmost) {
        task_t *leftmost = rb_entry(cfs_rq.rb_leftmost, task_t, cfs.run_node);
        cfs_rq.min_vruntime = leftmost->cfs.vruntime;
    }
}

/**
 * Enqueue task into CFS run queue (RB-tree)
 */
void cfs_enqueue_task(task_t *task) {
    rb_node_t **link = &cfs_rq.tasks_timeline.node;
    rb_node_t *parent = NULL;
    task_t *entry;
    int leftmost = 1;
    
    // Find insertion point
    while (*link) {
        parent = *link;
        entry = rb_entry(parent, task_t, cfs.run_node);
        
        if (task->cfs.vruntime < entry->cfs.vruntime) {
            link = &parent->left;
        } else {
            link = &parent->right;
            leftmost = 0;
        }
    }
    
    // Cache leftmost node
    if (leftmost)
        cfs_rq.rb_leftmost = &task->cfs.run_node;
    
    // Insert node
    rb_set_parent(&task->cfs.run_node, parent);
    rb_set_color(&task->cfs.run_node, RB_RED);
    task->cfs.run_node.left = NULL;
    task->cfs.run_node.right = NULL;
    
    *link = &task->cfs.run_node;
    rb_insert_color(&task->cfs.run_node, &cfs_rq.tasks_timeline);
    
    // Update run queue state
    task->cfs.on_rq = 1;
    cfs_rq.nr_running++;
    cfs_rq.load_weight += task->cfs.load_weight;
    
    // Set virtual runtime to at least min_vruntime
    if (task->cfs.vruntime < cfs_rq.min_vruntime)
        task->cfs.vruntime = cfs_rq.min_vruntime;
}

/**
 * Dequeue task from CFS run queue
 */
void cfs_dequeue_task(task_t *task) {
    if (!task->cfs.on_rq)
        return;
    
    // Update leftmost cache if needed
    if (cfs_rq.rb_leftmost == &task->cfs.run_node) {
        rb_node_t *next = rb_next(&task->cfs.run_node);
        cfs_rq.rb_leftmost = next;
    }
    
    // Remove from RB-tree
    rb_erase(&task->cfs.run_node, &cfs_rq.tasks_timeline);
    
    // Update run queue state
    task->cfs.on_rq = 0;
    cfs_rq.nr_running--;
    cfs_rq.load_weight -= task->cfs.load_weight;
}

/**
 * Pick next task to run (always leftmost = lowest vruntime)
 */
task_t *cfs_pick_next_task(void) {
    if (!cfs_rq.rb_leftmost)
        return NULL;
    
    task_t *next = rb_entry(cfs_rq.rb_leftmost, task_t, cfs.run_node);
    next->cfs.exec_start = timer_get_ticks();
    
    return next;
}

/**
 * Put previous task back
 */
void cfs_put_prev_task(task_t *task) {
    update_curr(task);
}

/**
 * Calculate task time slice
 */
uint64_t cfs_calculate_timeslice(task_t *task) {
    uint64_t slice;
    
    if (cfs_rq.nr_running > 1) {
        // timeslice = target_latency * (task_weight / total_weight)
        slice = (CFS_TARGET_LATENCY * task->cfs.load_weight) / cfs_rq.load_weight;
        
        // Enforce minimum granularity
        if (slice < CFS_MIN_GRANULARITY)
            slice = CFS_MIN_GRANULARITY;
    } else {
        // Only task, give full target latency
        slice = CFS_TARGET_LATENCY;
    }
    
    return slice;
}

/**
 * Scheduler tick for CFS
 */
void cfs_task_tick(task_t *task) {
    update_curr(task);
    
    // Check if task's time slice has expired
    uint64_t slice = cfs_calculate_timeslice(task);
    uint64_t runtime = task->cfs.sum_exec_runtime - 
                      (task->last_scheduled_time * timer_get_frequency());
    
    if (runtime >= slice) {
        // Time slice expired, need to reschedule
        task->flags |= TASK_FLAG_NEED_RESCHED;
    }
}
```

### Step 2.4: Integrate with Existing Scheduler

```c
// In kernel/task/scheduler.c

#include "../include/kernel/cfs.h"

// In scheduler_init():
void scheduler_init(void) {
    // ... existing code ...
    
    // Initialize CFS
    cfs_init();
    
    kernel_print("[SCHED] CFS scheduler initialized\n");
}

// In scheduler_schedule():
void scheduler_schedule(void) {
    // ... existing code ...
    
    // Use CFS to pick next task
    task_t *next = cfs_pick_next_task();
    
    if (next && next != current_task) {
        context_switch(next);
    }
}
```

### Step 2.5: Test CFS

Create test tasks with different priorities:

```c
// Test CFS fairness
void test_cfs(void) {
    // Create tasks with different nice values
    int task1 = task_create(worker_func, "worker-high", SCHED_NORMAL, 100); // nice -20
    int task2 = task_create(worker_func, "worker-normal", SCHED_NORMAL, 120); // nice 0
    int task3 = task_create(worker_func, "worker-low", SCHED_NORMAL, 139); // nice +19
    
    // Let them run
    task_sleep(10000);  // 10 seconds
    
    // Check statistics
    task_stats_t stats1, stats2, stats3;
    task_get_stats(task1, &stats1);
    task_get_stats(task2, &stats2);
    task_get_stats(task3, &stats3);
    
    // High priority should get more CPU time
    kernel_print("High priority CPU time: ");
    print_uint64(stats1.exec_time);
    kernel_print("\n");
    
    kernel_print("Normal priority CPU time: ");
    print_uint64(stats2.exec_time);
    kernel_print("\n");
    
    kernel_print("Low priority CPU time: ");
    print_uint64(stats3.exec_time);
    kernel_print("\n");
}
```

---

## Testing Guidelines

### Unit Testing

Create unit tests for each component:

```c
// tests/test_kaslr.c

#include "test_framework.h"
#include "../include/kernel/kaslr.h"

TEST(kaslr_entropy_generation) {
    uint64_t e1 = gather_entropy();
    uint64_t e2 = gather_entropy();
    
    // Entropy values should be different
    ASSERT_NE(e1, e2);
    
    // Should not be all zeros
    ASSERT_NE(e1, 0);
}

TEST(kaslr_base_calculation) {
    kaslr_init();
    uint64_t base = kaslr_get_base();
    
    // Base should be within valid range
    ASSERT_GE(base, KERNEL_BASE_MIN);
    ASSERT_LE(base, KERNEL_BASE_MAX);
    
    // Should be aligned to 2MB
    ASSERT_EQ(base % KASLR_ALIGNMENT, 0);
}

TEST(kaslr_randomness) {
    // Run multiple initializations
    uint64_t bases[10];
    
    for (int i = 0; i < 10; i++) {
        kaslr_init();
        bases[i] = kaslr_get_base();
    }
    
    // At least some values should be different
    int unique = count_unique(bases, 10);
    ASSERT_GT(unique, 1);
}
```

### Integration Testing

```bash
#!/bin/bash
# tests/integration/test_kaslr.sh

echo "Testing KASLR integration..."

# Build kernel
make clean && make ARCH=x86_64 || exit 1

# Run 10 times and collect kernel bases
for i in {1..10}; do
    timeout 10 qemu-system-x86_64 -drive format=raw,file=build/TocinOS.img \
        -serial stdio -display none 2>&1 | grep "KASLR" >> /tmp/kaslr_test.log
done

# Analyze results
python3 << EOF
import re

with open('/tmp/kaslr_test.log') as f:
    bases = []
    for line in f:
        match = re.search(r'Kernel base: 0x([0-9A-F]+)', line)
        if match:
            bases.append(int(match.group(1), 16))
    
    if len(bases) < 5:
        print("FAIL: Not enough boots recorded")
        exit(1)
    
    unique = len(set(bases))
    if unique < 2:
        print("FAIL: KASLR not randomizing (all bases same)")
        exit(1)
    
    print(f"PASS: {unique}/{len(bases)} unique kernel bases")
    print(f"Range: 0x{min(bases):X} - 0x{max(bases):X}")
EOF
```

---

## Performance Benchmarking

### Benchmark Framework

```c
// include/test/benchmark.h

typedef struct benchmark_result {
    const char *name;
    uint64_t iterations;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t avg_ns;
} bench_result_t;

#define BENCHMARK(name, iterations) \
    bench_result_t bench_##name(void)

// Run benchmark
void run_benchmark(bench_result_t (*benchmark)(void));

// Compare benchmarks
void compare_benchmarks(bench_result_t *baseline, bench_result_t *current);
```

### Example Benchmarks

```c
// tests/benchmarks/bench_scheduler.c

BENCHMARK(scheduler_context_switch, 10000) {
    bench_result_t result = {
        .name = "Context Switch",
        .iterations = 10000,
    };
    
    uint64_t start = rdtsc();
    
    for (uint64_t i = 0; i < result.iterations; i++) {
        task_yield();  // Force context switch
    }
    
    uint64_t end = rdtsc();
    result.total_ns = tsc_to_ns(end - start);
    result.avg_ns = result.total_ns / result.iterations;
    
    return result;
}

BENCHMARK(cfs_enqueue_dequeue, 100000) {
    bench_result_t result = {
        .name = "CFS Enqueue/Dequeue",
        .iterations = 100000,
    };
    
    task_t *task = create_test_task();
    
    uint64_t start = rdtsc();
    
    for (uint64_t i = 0; i < result.iterations; i++) {
        cfs_enqueue_task(task);
        cfs_dequeue_task(task);
    }
    
    uint64_t end = rdtsc();
    result.total_ns = tsc_to_ns(end - start);
    result.avg_ns = result.total_ns / result.iterations;
    
    return result;
}
```

---

## Documentation Standards

### Code Comments

```c
/**
 * Function description (what it does)
 * 
 * @param param1 Description of parameter 1
 * @param param2 Description of parameter 2
 * @return Description of return value
 * 
 * @note Additional notes or warnings
 * @see Related functions
 * 
 * Example usage:
 * @code
 * int result = my_function(10, 20);
 * @endcode
 */
int my_function(int param1, int param2) {
    // Implementation
}
```

### Feature Documentation

For each major feature, create documentation:

```markdown
# Feature Name

## Overview
Brief description of the feature and its purpose.

## Design
Architecture and design decisions.

## Implementation
Key implementation details.

## API
Public functions and their usage.

## Testing
How to test the feature.

## Performance
Performance characteristics and benchmarks.

## References
Links to related documentation and resources.
```

---

## Code Review Checklist

Before submitting a pull request:

- [ ] Code compiles without warnings
- [ ] All tests pass
- [ ] New tests added for new functionality
- [ ] Code follows style guidelines
- [ ] Comments and documentation updated
- [ ] Performance benchmarks run (if applicable)
- [ ] No memory leaks (checked with valgrind/tools)
- [ ] Security considerations addressed
- [ ] Backward compatibility maintained
- [ ] Commit messages are descriptive

---

## Next Steps

1. **Choose a feature**: Pick from Phase 1 (KASLR, CFS, etc.)
2. **Create branch**: `git checkout -b feature/name`
3. **Implement incrementally**: Small, testable changes
4. **Test thoroughly**: Unit tests, integration tests, benchmarks
5. **Document**: Comments, API docs, feature docs
6. **Submit PR**: With clear description and test results
7. **Iterate**: Address review feedback

---

## Getting Help

- **Documentation**: Read ARCHITECTURE.md, FEATURES.md, VISION.md
- **Community**: Join our discussion forums/chat
- **Issues**: Check GitHub issues for similar work
- **Mentorship**: Ask experienced contributors for guidance

---

*Happy coding! Let's build the best OS together!*
