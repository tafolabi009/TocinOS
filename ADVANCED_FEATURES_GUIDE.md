# TocinOS Advanced Features Implementation Guide

This document provides guidance on using and extending the advanced features framework implemented in TocinOS.

## Table of Contents

1. [UEFI Boot Support](#uefi-boot-support)
2. [KASLR (Kernel Address Space Layout Randomization)](#kaslr)
3. [CFS Scheduler](#cfs-scheduler)
4. [Advanced Memory Management](#advanced-memory-management)
5. [TocinFS Filesystem](#tocinfs-filesystem)
6. [Security Framework](#security-framework)

---

## UEFI Boot Support

### Overview
TocinOS now includes a modular UEFI bootloader framework with support for modern boot features.

### Components

#### Graphics Module (`boot/uefi/graphics.c`)
Provides Graphics Output Protocol (GOP) support for high-resolution framebuffers.

**Key Functions:**
- `gop_init()` - Initialize GOP
- `gop_set_mode()` - Set graphics mode
- `gop_get_framebuffer()` - Get framebuffer information

#### Filesystem Module (`boot/uefi/filesystem.c`)
Handles kernel loading from EFI partitions.

**Key Functions:**
- `load_kernel_file()` - Load kernel from EFI partition
- `read_file()` - Read file data
- `get_file_info()` - Get file metadata

#### Memory Module (`boot/uefi/memory.c`)
Manages UEFI memory map and allocation.

**Key Functions:**
- `get_uefi_memory_map()` - Retrieve UEFI memory map
- `allocate_pages()` - Allocate memory pages
- `convert_memory_map()` - Convert to kernel format

#### Secure Boot Module (`boot/uefi/secure_boot.c`)
Verifies bootloader and kernel signatures.

**Key Functions:**
- `is_secure_boot_enabled()` - Check if secure boot is active
- `verify_kernel_signature()` - Verify kernel signature

### Usage

To compile the UEFI bootloader (requires gnu-efi):
```bash
# Install gnu-efi
sudo apt-get install gnu-efi

# Build UEFI bootloader
cd boot/uefi
gcc -I/usr/include/efi -fpic -ffreestanding \
    -fno-stack-protector -fno-builtin -Wl,-dll \
    -shared -Wl,-Bsymbolic -L/usr/lib -lefi \
    -o bootx64.efi *.c
```

---

## KASLR

### Overview
Kernel Address Space Layout Randomization adds security by randomizing the kernel's load address.

### Features
- Hardware RNG support (RDRAND/RDSEED)
- Timer jitter entropy
- Memory map and CPU feature hashing
- 10 bits of entropy (1024 positions)
- 2MB alignment

### API

**Header:** `include/kernel/kaslr.h`

**Functions:**
```c
// Initialize KASLR (call early in boot)
void kaslr_init(void);

// Get randomized kernel base address
uint64_t kaslr_get_base(void);

// Check if KASLR is enabled
int kaslr_is_enabled(void);

// Get KASLR information
kaslr_info_t *kaslr_get_info(void);

// Disable KASLR (for debugging)
void kaslr_disable(void);
```

### Example Usage
```c
#include "kernel/kaslr.h"

void early_kernel_init(void) {
    // Initialize KASLR
    kaslr_init();
    
    // Get randomized base address
    uint64_t base = kaslr_get_base();
    
    // Use base address for kernel relocation
    relocate_kernel(base);
}
```

---

## CFS Scheduler

### Overview
The Completely Fair Scheduler provides fair CPU time distribution using a red-black tree.

### Features
- O(log n) task selection
- Virtual runtime tracking
- Priority weighting (nice -20 to +19)
- Group scheduling support
- Load balancing

### API

**Header:** `include/kernel/cfs.h`

**Functions:**
```c
// Initialize CFS
void cfs_init(void);

// Add task to runqueue
void cfs_enqueue_task(cfs_rq_t *cfs_rq, cfs_task_t *task);

// Remove task from runqueue
void cfs_dequeue_task(cfs_rq_t *cfs_rq, cfs_task_t *task);

// Pick next task to run
cfs_task_t *cfs_pick_next_task(cfs_rq_t *cfs_rq);

// Update task runtime
void cfs_update_curr(cfs_rq_t *cfs_rq, cfs_task_t *task, uint64_t delta_exec);
```

### Example Usage
```c
#include "kernel/cfs.h"

void scheduler_tick(void) {
    cfs_rq_t *rq = get_current_runqueue();
    cfs_task_t *current = get_current_task();
    
    // Update current task's runtime
    uint64_t delta = get_time_since_last_tick();
    cfs_update_curr(rq, current, delta);
    
    // Check if we need to reschedule
    cfs_task_t *next = cfs_pick_next_task(rq);
    if (next != current) {
        context_switch(next);
    }
}
```

---

## Advanced Memory Management

### zswap - Memory Compression

**Header:** `include/kernel/zswap.h`

Compresses pages before swapping to reduce I/O.

**Functions:**
```c
void zswap_init(void);
int zswap_store_page(uint64_t offset, const void *page);
int zswap_load_page(uint64_t offset, void *page);
void zswap_get_stats(uint64_t *stored, uint64_t *compressed_size, 
                     uint64_t *uncompressed_size);
```

### NUMA Support

**Header:** `include/kernel/numa.h`

Non-Uniform Memory Access support for multi-socket systems.

**Functions:**
```c
void numa_init(void);
void *numa_alloc_onnode(uint64_t size, int node);
int numa_cpu_to_node(int cpu);
int numa_migrate_page(void *page, int target_node);
```

### Huge Pages

**Header:** `include/kernel/hugepage.h`

Support for 2MB and 1GB pages to reduce TLB misses.

**Functions:**
```c
void hugepage_init(void);
void *alloc_huge_page(int order);  // order 9 = 2MB, order 18 = 1GB
void free_huge_page(void *addr, int order);
void thp_enable(void);  // Enable Transparent Huge Pages
```

---

## TocinFS Filesystem

### Overview
Modern filesystem with Copy-on-Write, snapshots, and compression.

### Features
- Copy-on-Write (CoW) for atomic operations
- Instant snapshots (O(1))
- Transparent compression (LZ4, ZSTD, LZO)
- Data deduplication
- Online defragmentation
- Checksumming

### API

**Header:** `include/fs/tocinfs.h`

**Functions:**
```c
// Initialize filesystem
int tocinfs_init(void);

// Mount/unmount
int tocinfs_mount(const char *device, const char *mountpoint);
int tocinfs_unmount(const char *mountpoint);

// File operations
int tocinfs_create(const char *path, uint32_t mode);
int tocinfs_read(int fd, void *buffer, uint64_t size);
int tocinfs_write(int fd, const void *buffer, uint64_t size);

// Snapshot management
int tocinfs_create_snapshot(const char *source, const char *snapshot_name);
int tocinfs_rollback_snapshot(const char *snapshot_name);
int tocinfs_delete_snapshot(const char *snapshot_name);

// Maintenance
int tocinfs_defrag_file(const char *path);
```

### Example Usage
```c
#include "fs/tocinfs.h"

void example_snapshot(void) {
    // Mount filesystem
    tocinfs_mount("/dev/sda1", "/mnt");
    
    // Create snapshot before making changes
    tocinfs_create_snapshot("/mnt", "before_update");
    
    // Make changes...
    
    // If something goes wrong, rollback
    tocinfs_rollback_snapshot("before_update");
}
```

---

## Security Framework

### Capabilities

**Header:** `include/kernel/capability.h`

Linux-compatible capability system.

**Functions:**
```c
void cap_init(void);
int capable(int cap);  // Check current process
int cap_drop(int cap);
int cap_raise(int cap);
```

**Example:**
```c
// Check if process can bind to privileged ports
if (!capable(CAP_NET_BIND_SERVICE)) {
    return -EPERM;
}
```

### Seccomp

**Header:** `include/kernel/seccomp.h`

System call filtering using BPF.

**Functions:**
```c
void seccomp_init(void);
int seccomp_set_mode_filter(seccomp_filter_t *filter);
int seccomp_set_mode_strict(void);
```

**Example:**
```c
// Allow only read, write, exit
bpf_insn_t filter[] = {
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 
             offsetof(seccomp_data_t, nr)),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_read, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_write, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_exit, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL),
};
```

### AppArmor

**Header:** `include/kernel/apparmor.h`

Mandatory Access Control (MAC) with profiles.

**Functions:**
```c
void aa_init(void);
int aa_load_profile(const char *profile_data);
int aa_file_permission(aa_profile_t *profile, const char *path, 
                       int access_mode);
int aa_attach_profile(void *process, aa_profile_t *profile);
```

---

## Implementation Status

All frameworks are implemented with:
- ✅ Header files with complete API definitions
- ✅ Stub implementations for future development
- ✅ Documentation
- ⏳ Full implementations pending (placeholders in place)

## Next Steps

1. **UEFI Bootloader**: Complete gnu-efi integration
2. **KASLR**: Integrate with kernel loader
3. **CFS**: Replace current round-robin scheduler
4. **Memory**: Implement compression algorithms
5. **TocinFS**: Implement B-tree operations
6. **Security**: Integrate with process management

## Contributing

When implementing these features fully:
1. Follow the existing API signatures
2. Add comprehensive error checking
3. Write unit tests
4. Update this documentation
5. Maintain backward compatibility

---

For more information, see:
- [ADVANCED_FEATURES_ROADMAP.md](ADVANCED_FEATURES_ROADMAP.md)
- [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md)
- [ARCHITECTURE.md](ARCHITECTURE.md)
