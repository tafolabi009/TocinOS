/**
 * TocinOS Advanced Memory Management Header
 * 
 * Features:
 * - Buddy allocator for efficient power-of-2 allocations
 * - Slab allocator for kernel objects
 * - Demand paging with page fault handling
 * - Copy-on-Write (COW) support
 * - Memory-mapped files framework
 * - NUMA-aware allocation (framework)
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

// Page size definitions
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

// Page flags
#define PAGE_PRESENT    0x001
#define PAGE_WRITE      0x002
#define PAGE_USER       0x004
#define PAGE_PWT        0x008  // Page Write-Through
#define PAGE_PCD        0x010  // Page Cache Disable
#define PAGE_ACCESSED   0x020
#define PAGE_DIRTY      0x040
#define PAGE_PAT        0x080  // Page Attribute Table
#define PAGE_GLOBAL     0x100
#define PAGE_COW        0x200  // Copy-on-Write (custom flag)

// ==================== PHYSICAL MEMORY MANAGER (PMM) ====================

// Basic PMM functions
void pmm_init(void);
unsigned int pmm_alloc_page(void);
void pmm_free_page(unsigned int address);
void pmm_set_page_used(unsigned int page);
unsigned int pmm_get_total_pages(void);
unsigned int pmm_get_used_pages(void);
unsigned int pmm_get_free_pages(void);

// ==================== BUDDY ALLOCATOR ====================

#define MAX_ORDER 11  // 2^11 * 4KB = 8MB max allocation

typedef struct buddy_page {
    struct buddy_page *next;
    struct buddy_page *prev;
    uint32_t order;          // Order in buddy system
    uint32_t flags;          // Page flags
    uint32_t ref_count;      // Reference counter
} buddy_page_t;

typedef struct {
    buddy_page_t *free_lists[MAX_ORDER];
    uint32_t free_pages[MAX_ORDER];
    uint64_t total_free;
    uint64_t total_used;
} buddy_allocator_t;

// Buddy allocator functions
void buddy_init(void);
void* buddy_alloc(uint32_t order);
void buddy_free(void *addr, uint32_t order);
void* buddy_alloc_pages(uint32_t num_pages);
void buddy_free_pages(void *addr, uint32_t num_pages);
uint32_t buddy_get_order(uint32_t size);

// ==================== SLAB ALLOCATOR ====================

#define SLAB_NAME_LEN 32
#define MAX_SLABS 64

typedef struct slab {
    struct slab *next;
    void *free_list;         // Free objects in this slab
    uint32_t inuse;          // Number of used objects
    uint32_t total;          // Total objects in slab
} slab_t;

typedef struct {
    char name[SLAB_NAME_LEN];
    uint32_t obj_size;       // Size of each object
    uint32_t align;          // Alignment requirement
    uint32_t flags;          // Slab flags
    slab_t *slabs_full;      // Fully allocated slabs
    slab_t *slabs_partial;   // Partially allocated slabs
    slab_t *slabs_free;      // Empty slabs
    uint32_t num_slabs;      // Total slabs
    uint32_t num_objs;       // Total objects
    uint32_t num_active;     // Active objects
    void (*ctor)(void *);    // Constructor
    void (*dtor)(void *);    // Destructor
} kmem_cache_t;

// Slab allocator functions
void slab_init(void);
kmem_cache_t* kmem_cache_create(const char *name, uint32_t size, uint32_t align,
                                uint32_t flags, void (*ctor)(void *), void (*dtor)(void *));
void kmem_cache_destroy(kmem_cache_t *cache);
void* kmem_cache_alloc(kmem_cache_t *cache);
void kmem_cache_free(kmem_cache_t *cache, void *obj);

// General purpose kernel memory allocation
void* kmalloc(uint32_t size);
void* kzalloc(uint32_t size);
void kfree(void *ptr);

// ==================== VIRTUAL MEMORY MANAGER (VMM) ====================

// VMM functions
void vmm_init(void);
void vmm_map_page(unsigned int virtual_addr, unsigned int physical_addr, unsigned int flags);
void vmm_unmap_page(unsigned int virtual_addr);
void vmm_switch_directory(unsigned int directory_phys);
unsigned int vmm_get_current_directory(void);

// Advanced VMM functions
int vmm_map_range(uint32_t virt_start, uint32_t phys_start, uint32_t size, uint32_t flags);
int vmm_unmap_range(uint32_t virt_start, uint32_t size);
uint32_t vmm_virt_to_phys(uint32_t virt_addr);
int vmm_is_mapped(uint32_t virt_addr);
int vmm_get_page_flags(uint32_t virt_addr);
int vmm_set_page_flags(uint32_t virt_addr, uint32_t flags);

// Page directory management
uint32_t vmm_create_address_space(void);
void vmm_destroy_address_space(uint32_t directory_phys);
uint32_t vmm_clone_address_space(uint32_t source_dir);

// Copy-on-Write support
int vmm_mark_cow(uint32_t virt_addr);
int vmm_handle_cow_fault(uint32_t virt_addr);

// ==================== PAGE FAULT HANDLING ====================

typedef struct {
    uint32_t fault_addr;     // Faulting address
    uint32_t error_code;     // Error code from CPU
    uint32_t eip;            // Instruction pointer
    uint32_t cr2;            // CR2 register value
} page_fault_info_t;

// Page fault error code flags
#define PF_PRESENT  0x01     // Page not present
#define PF_WRITE    0x02     // Write fault
#define PF_USER     0x04     // User mode fault
#define PF_RESERVED 0x08     // Reserved bit violation
#define PF_FETCH    0x10     // Instruction fetch

void page_fault_handler(page_fault_info_t *info);
void page_fault_register_handler(void (*handler)(page_fault_info_t *));

// ==================== MEMORY-MAPPED FILES ====================

#define MAX_MMAPS 128

typedef struct {
    uint32_t virt_addr;      // Virtual address
    uint32_t size;           // Size in bytes
    uint32_t offset;         // File offset
    uint32_t flags;          // Mapping flags
    int fd;                  // File descriptor
    int task_id;             // Owner task
} mmap_region_t;

// Memory mapping flags
#define MMAP_PROT_READ   0x1
#define MMAP_PROT_WRITE  0x2
#define MMAP_PROT_EXEC   0x4
#define MMAP_SHARED      0x10
#define MMAP_PRIVATE     0x20
#define MMAP_FIXED       0x40
#define MMAP_ANONYMOUS   0x80

// Memory mapping functions
void* mmap(void *addr, uint32_t length, int prot, int flags, int fd, uint32_t offset);
int munmap(void *addr, uint32_t length);
int mprotect(void *addr, uint32_t length, int prot);
int msync(void *addr, uint32_t length, int flags);

// ==================== MEMORY STATISTICS ====================

typedef struct {
    uint64_t total_memory;
    uint64_t used_memory;
    uint64_t free_memory;
    uint64_t cached_memory;
    uint64_t buffered_memory;
    uint64_t slab_memory;
    uint32_t page_faults;
    uint32_t major_faults;
    uint32_t cow_faults;
} mem_stats_t;

void mem_get_stats(mem_stats_t *stats);
void mem_print_stats(void);

// ==================== NUMA SUPPORT (Framework) ====================

#define MAX_NUMA_NODES 8

typedef struct {
    uint32_t node_id;
    uint64_t start_addr;
    uint64_t size;
    uint64_t free_pages;
    buddy_allocator_t *buddy;
} numa_node_t;

void numa_init(void);
void* numa_alloc_on_node(uint32_t node_id, uint32_t order);
uint32_t numa_get_current_node(void);
uint32_t numa_get_num_nodes(void);

#endif // MEMORY_H
