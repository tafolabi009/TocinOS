/**
 * @file memory.h
 * @brief TocinOS Advanced Memory Management
 * @author TocinOS Team
 * 
 * This file contains the comprehensive memory management API for TocinOS,
 * including physical memory management (PMM), virtual memory management (VMM),
 * buddy allocator, slab allocator, and NUMA support.
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
#include "../boot/tocinboot.h"

/** @defgroup PageDefs Page Size Definitions
 * @{
 */
#define PAGE_SIZE 4096          /**< Standard page size (4KB) */
#define PAGE_SHIFT 12           /**< Page size shift (2^12 = 4096) */
/** @} */

/** @defgroup PageFlags Page Table Flags
 * @{
 */
#define PAGE_PRESENT    0x001   /**< Page is present in memory */
#define PAGE_WRITE      0x002   /**< Page is writable */
#define PAGE_USER       0x004   /**< Page is user-accessible */
#define PAGE_PWT        0x008   /**< Page Write-Through */
#define PAGE_PCD        0x010   /**< Page Cache Disable */
#define PAGE_ACCESSED   0x020   /**< Page has been accessed */
#define PAGE_DIRTY      0x040   /**< Page has been written to */
#define PAGE_PAT        0x080   /**< Page Attribute Table */
#define PAGE_GLOBAL     0x100   /**< Global page (not flushed on CR3 reload) */
#define PAGE_COW        0x200   /**< Copy-on-Write (custom flag) */
/** @} */

// ==================== PHYSICAL MEMORY MANAGER (PMM) ====================

/**
 * @brief Initialize the Physical Memory Manager
 * 
 * Sets up the page frame allocator and bitmap for tracking free pages.
 */
void pmm_init(void);

/**
 * @brief Initialize the PMM from a TocinBoot memory map (conservative v1)
 *
 * Same 128 MiB bitmap as pmm_init() (which it calls first, so the first
 * 2 MiB stay reserved), but additionally marks as used, within the 128 MiB
 * window:
 *  - every memmap region that is NOT type TOCINBOOT_MEM_USABLE — including
 *    BOOTLOADER regions (reclaim is an M2 item, spec §4.2)
 *  - the framebuffer range (fb_pitch * fb_height bytes), if handed off
 *
 * @param info Validated tocinboot_info (e.g. bootinfo_get()); NULL degrades
 *             to plain pmm_init() behavior.
 */
void pmm_init_from_bootinfo(const tocinboot_info *info);

/**
 * @brief Allocate a physical page
 * 
 * Allocates a single 4KB physical page from the page frame allocator.
 * The page is marked as used and cannot be allocated again until freed.
 * 
 * @return Physical address of allocated page, or 0 if out of memory
 * 
 * @note The returned address is a physical address, not virtual.
 * @see pmm_free_page()
 * 
 * @code
 * unsigned int page = pmm_alloc_page();
 * if (page != 0) {
 *     // Use page
 *     pmm_free_page(page);
 * }
 * @endcode
 */
unsigned int pmm_alloc_page(void);

/**
 * @brief Free a physical page
 * 
 * Returns a previously allocated page to the free page pool.
 * 
 * @param address Physical address of the page to free
 * 
 * @see pmm_alloc_page()
 */
/**
 * @brief Free a physical page
 * 
 * Returns a previously allocated page to the free page pool.
 * 
 * @param address Physical address of the page to free
 * 
 * @see pmm_alloc_page()
 */
void pmm_free_page(unsigned int address);

/**
 * @brief Mark a physical page as used
 * 
 * Marks a page as used in the page bitmap without actually allocating it.
 * Useful for reserving memory regions (e.g., kernel, BIOS).
 * 
 * @param page Page number (not address) to mark as used
 */
void pmm_set_page_used(unsigned int page);

/**
 * @brief Get total number of pages
 * 
 * @return Total number of physical pages in the system
 */
unsigned int pmm_get_total_pages(void);

/**
 * @brief Get number of used pages
 * 
 * @return Number of pages currently allocated
 */
unsigned int pmm_get_used_pages(void);

/**
 * @brief Get number of free pages
 * 
 * @return Number of pages available for allocation
 */
unsigned int pmm_get_free_pages(void);

// ==================== BUDDY ALLOCATOR ====================
/*
 * Physical page-range allocator for the HIGH region of RAM.
 *
 * Physical-memory ownership split (kernel.c wires this up at boot):
 *
 *   [0, 16MB)      PMM bitmap allocator (pmm_alloc_page): kernel image,
 *                  page tables, and every legacy single-page user.
 *   [16MB, top)    Buddy allocator: power-of-2 page-range allocations and
 *                  the slab backing store (kmalloc). "top" is derived from
 *                  the TocinBoot memory map (usable RAM only, non-usable
 *                  regions and the framebuffer are excluded), capped at
 *                  128MB — the PMM's window. Fallback without bootinfo:
 *                  the fixed range [16MB, 112MB) inside the 128MB
 *                  assumption.
 *
 * At boot, kernel.c marks every buddy-managed page as used in the PMM
 * bitmap, so the two allocators can never hand out the same frame.
 *
 * Orders are 0..MAX_ORDER-1; an order-k block is (4KB << k), so the
 * largest block is 4MB (order 10). The allocator never dereferences the
 * memory it manages (all bookkeeping lives in a static descriptor table),
 * so it is safe to initialize before the region is virtually mapped.
 */

#define MAX_ORDER 11                     /**< Orders 0..10 => max block 4MB */
#define BUDDY_REGION_START 0x01000000u   /**< 16MB: buddy space begins here */
#define BUDDY_REGION_LIMIT 0x08000000u   /**< 128MB: hard cap (PMM window)  */
#define BUDDY_FALLBACK_END 0x07000000u   /**< 112MB: no-bootinfo fallback   */

/* Opaque handle kept for the NUMA framework section below. */
typedef struct buddy_allocator buddy_allocator_t;

/**
 * @brief Initialize the buddy allocator over [start_addr, end_addr).
 *
 * start_addr is aligned up and end_addr down to page boundaries; the span
 * is clipped to the compile-time capacity (BUDDY_REGION_LIMIT -
 * BUDDY_REGION_START bytes). Every page in the span becomes allocatable.
 * Re-initialization is allowed (host unit tests rely on it).
 *
 * @return Number of managed (allocatable) pages, or -1 on a bad range.
 */
int buddy_init(uint32_t start_addr, uint32_t end_addr);

/**
 * @brief Initialize the buddy allocator from a TocinBoot memory map.
 *
 * Region is [16MB, 128MB); only pages fully covered by USABLE memmap
 * entries (minus any non-USABLE overlap and the framebuffer range) become
 * allocatable. Must be called while physical addresses are still directly
 * dereferenceable (before vmm_init), because the memmap entries live at
 * their original physical address. NULL/invalid info degrades to
 * buddy_init(BUDDY_REGION_START, BUDDY_FALLBACK_END).
 *
 * @return Number of managed (allocatable) pages, or -1 on failure.
 */
int buddy_init_from_bootinfo(const tocinboot_info *info);

/** Allocate a block of (PAGE_SIZE << order) bytes; NULL if impossible. */
void* buddy_alloc(uint32_t order);

/**
 * @brief Free a block previously returned by buddy_alloc(order).
 *
 * Validated: addr must be the exact head of a live allocation of exactly
 * this order. Double frees, mid-block pointers, foreign addresses and
 * wrong orders are rejected.
 *
 * @return 0 on success, -1 if the free was rejected.
 */
int buddy_free(void *addr, uint32_t order);

/**
 * @brief Free a block using the order recorded at allocation time.
 * @return The freed block's order (>= 0), or -1 if addr is not the head
 *         of a live allocation. Used by kfree() for large allocations.
 */
int buddy_free_block(void *addr);

/** Allocate >= num_pages contiguous pages (rounded up to a power of 2). */
void* buddy_alloc_pages(uint32_t num_pages);

/** Free a buddy_alloc_pages(num_pages) block. 0 on success, -1 rejected. */
int buddy_free_pages(void *addr, uint32_t num_pages);

/**
 * @brief Smallest order whose block holds size bytes.
 * @return MAX_ORDER (an invalid order) when size exceeds the largest block.
 */
uint32_t buddy_get_order(uint32_t size);

/** 1 if addr lies inside the buddy region span, else 0. */
int buddy_owns(const void *addr);

/** 1 if the page at addr is buddy-managed (inside span AND allocatable). */
int buddy_addr_is_managed(uint32_t addr);

/** Pages the buddy manages (allocatable capacity, holes excluded). */
uint32_t buddy_get_managed_pages(void);

/** Pages currently free. */
uint32_t buddy_get_free_page_count(void);

/** Number of free blocks currently on the given order's free list. */
uint32_t buddy_free_blocks_of_order(uint32_t order);

/** Region span actually configured (page-aligned, after clipping). */
void buddy_get_region(uint32_t *start_addr, uint32_t *end_addr);

// ==================== SLAB ALLOCATOR ====================
/*
 * Object caches on top of the buddy allocator. Every slab is one buddy
 * order-0 page whose first bytes are the slab_t header (magic + owning
 * cache back-pointer); objects follow at a 16-byte-aligned offset. That
 * header is how kfree() finds the owning cache from a bare pointer:
 *
 *   kmalloc(size <= 1024)  -> size-class cache (8..1024), pointer is
 *                             never page-aligned (objects sit after the
 *                             in-page header).
 *   kmalloc(size >  1024)  -> buddy_alloc() directly, pointer is always
 *                             page-aligned.
 *   kfree(ptr)             -> page-aligned ptr: buddy_free_block();
 *                             otherwise: slab header at the page base.
 *
 * Guards: slab pages carry SLAB_MAGIC; kmem_cache_free() validates the
 * magic, the owning cache, the object offset, and scans the slab free
 * list to reject double frees. buddy_free_block() rejects double frees
 * of large allocations.
 */

#define SLAB_NAME_LEN 32
#define MAX_SLABS 64
#define SLAB_MAGIC 0x51ABCAFEu           /**< live slab page marker */
#define KMALLOC_MAX_SLAB_SIZE 1024u      /**< larger goes straight to buddy */

struct kmem_cache;

typedef struct slab {
    uint32_t magic;              /**< SLAB_MAGIC while the slab is live */
    struct kmem_cache *cache;    /**< owning cache (kfree lookup) */
    struct slab *next;           /**< cache list link */
    struct slab *prev;           /**< cache list link */
    void *free_list;             /**< first free object in this slab */
    uint32_t inuse;              /**< number of allocated objects */
    uint32_t total;              /**< object capacity of this slab */
    uint32_t list_id;            /**< which cache list this slab is on */
} slab_t;

typedef struct kmem_cache {
    char name[SLAB_NAME_LEN];
    uint32_t obj_size;           /**< aligned object size */
    uint32_t align;              /**< alignment requirement */
    uint32_t flags;              /**< unused in v1 */
    slab_t *slabs_full;          /**< slabs with no free object */
    slab_t *slabs_partial;       /**< slabs with free and used objects */
    slab_t *slabs_free;          /**< empty slabs (at most one is kept) */
    uint32_t num_slabs;          /**< slab pages owned by this cache */
    uint32_t num_active;         /**< live objects across all slabs */
    void (*ctor)(void *);        /**< called on every allocation */
    void (*dtor)(void *);        /**< called on every (valid) free */
} kmem_cache_t;

// Slab allocator functions
void slab_init(void);
kmem_cache_t* kmem_cache_create(const char *name, uint32_t size, uint32_t align,
                                uint32_t flags, void (*ctor)(void *), void (*dtor)(void *));
void kmem_cache_destroy(kmem_cache_t *cache);
void* kmem_cache_alloc(kmem_cache_t *cache);

/** @return 0 on success, -1 when the free is rejected (bad pointer, wrong
 *  cache, or double free). */
int kmem_cache_free(kmem_cache_t *cache, void *obj);

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
unsigned int vmm_get_physical(unsigned int virtual_addr);

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
    uint32_t esp;            // Stack pointer
    uint32_t cs;             // Code segment
} page_fault_info_t;

// Page fault error code flags (x86)
#define PF_PRESENT  0x01     // Page was present (protection violation vs not-present)
#define PF_WRITE    0x02     // Write fault (vs read fault)
#define PF_USER     0x04     // User mode fault (vs kernel mode)
#define PF_RESERVED 0x08     // Reserved bit violation
#define PF_FETCH    0x10     // Instruction fetch fault (NX violation)

// Page fault handler registration
void page_fault_handler(page_fault_info_t *info);
void page_fault_register_handler(void (*handler)(page_fault_info_t *));

// ==================== VIRTUAL MEMORY AREAS (VMA) ====================

/** @defgroup VMAFlags VMA Protection and Type Flags
 * @{
 */
#define VM_READ         0x001   /**< VMA is readable */
#define VM_WRITE        0x002   /**< VMA is writable */
#define VM_EXEC         0x004   /**< VMA is executable */
#define VM_SHARED       0x008   /**< Changes are shared */
#define VM_PRIVATE      0x010   /**< Private (copy-on-write) */
#define VM_ANONYMOUS    0x020   /**< No file backing */
#define VM_FILE         0x040   /**< File-backed mapping */
#define VM_LAZY         0x080   /**< Demand paging enabled */
#define VM_LOCKED       0x100   /**< Pages locked in memory */
#define VM_GROWSDOWN    0x200   /**< Stack-like growth direction */
#define VM_DONTCOPY     0x400   /**< Don't copy on fork */
#define VM_STACK        0x800   /**< This is a stack VMA */
/** @} */

/**
 * @brief Virtual Memory Area descriptor
 * 
 * Represents a contiguous region of virtual memory with consistent
 * permissions and backing. VMAs are organized in a linked list per process.
 */
typedef struct vma {
    uint32_t vm_start;          /**< Start virtual address (page aligned) */
    uint32_t vm_end;            /**< End virtual address (exclusive, page aligned) */
    uint32_t vm_flags;          /**< VMA flags (VM_READ, VM_WRITE, etc.) */
    
    /* File backing (if VM_FILE is set) */
    uint32_t vm_file_inode;     /**< Inode number of backing file */
    uint32_t vm_file_offset;    /**< Offset in file */
    int      vm_fd;             /**< File descriptor */
    
    /* Swap information (if pages are swapped out) */
    uint32_t swap_count;        /**< Number of swapped pages in this VMA */
    
    /* Reference counting */
    uint32_t ref_count;         /**< Reference count for shared mappings */
    
    /* Linked list */
    struct vma *vm_next;        /**< Next VMA in list */
    struct vma *vm_prev;        /**< Previous VMA in list */
} vma_t;

/**
 * @brief Memory descriptor for a process
 * 
 * Contains all virtual memory information for a single process.
 */
typedef struct mm_struct {
    vma_t    *vmas;             /**< Head of VMA linked list */
    uint32_t  vma_count;        /**< Number of VMAs */
    
    uint32_t  page_directory;   /**< Physical address of page directory */
    
    /* Memory regions */
    uint32_t  code_start;       /**< Start of code segment */
    uint32_t  code_end;         /**< End of code segment */
    uint32_t  data_start;       /**< Start of data segment */
    uint32_t  data_end;         /**< End of data/BSS */
    uint32_t  heap_start;       /**< Start of heap (brk) */
    uint32_t  heap_end;         /**< Current heap end (brk) */
    uint32_t  stack_start;      /**< Stack start (bottom) */
    uint32_t  stack_end;        /**< Stack end (top) */
    
    /* mmap region */
    uint32_t  mmap_base;        /**< Base address for mmap allocations */
    
    /* Statistics */
    uint32_t  total_vm;         /**< Total virtual memory */
    uint32_t  rss;              /**< Resident set size (pages in RAM) */
    uint32_t  shared;           /**< Shared pages */
    uint32_t  locked;           /**< Locked pages */
    
    /* Reference count */
    uint32_t  ref_count;        /**< Reference count for mm_struct */
} mm_struct_t;

// VMA management functions
vma_t* vma_create(uint32_t start, uint32_t end, uint32_t flags);
void   vma_destroy(vma_t *vma);
vma_t* vma_find(mm_struct_t *mm, uint32_t addr);
int    vma_insert(mm_struct_t *mm, vma_t *vma);
int    vma_remove(mm_struct_t *mm, vma_t *vma);
int    vma_merge(mm_struct_t *mm, vma_t *vma);

// MM struct management
mm_struct_t* mm_create(void);
void         mm_destroy(mm_struct_t *mm);
mm_struct_t* mm_clone(mm_struct_t *src);
void         mm_release(mm_struct_t *mm);

// ==================== MEMORY-MAPPED FILES ====================

#define MAX_MMAPS 256

// Memory mapping flags (POSIX compatible)
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_NORESERVE   0x40
#define MAP_GROWSDOWN   0x100
#define MAP_LOCKED      0x200
#define MAP_STACK       0x400

#define MAP_FAILED      ((void *)-1)

// Memory mapping functions
void* sys_mmap(void *addr, uint32_t length, int prot, int flags, int fd, uint32_t offset);
int   sys_munmap(void *addr, uint32_t length);
int   sys_mprotect(void *addr, uint32_t length, int prot);
int   sys_msync(void *addr, uint32_t length, int flags);
void* sys_brk(void *addr);

// Legacy function names (wrapper macros)
#define mmap(addr, len, prot, flags, fd, off)  sys_mmap(addr, len, prot, flags, fd, off)
#define munmap(addr, len)                       sys_munmap(addr, len)
#define mprotect(addr, len, prot)               sys_mprotect(addr, len, prot)
#define msync(addr, len, flags)                 sys_msync(addr, len, flags)

// ==================== VMM PAGE CACHE ====================
// This is a separate page cache for demand paging, distinct from fs_cache.h

#define VMM_PAGE_CACHE_HASH_BITS    8
#define VMM_PAGE_CACHE_HASH_SIZE    (1 << VMM_PAGE_CACHE_HASH_BITS)

/**
 * @brief VMM page cache entry (for demand paging)
 * 
 * Caches a single page of file data for memory-mapped files.
 */
typedef struct vmm_page_cache_entry {
    uint32_t inode;             /**< Inode number */
    uint32_t offset;            /**< Page offset in file (in pages) */
    uint32_t phys_addr;         /**< Physical address of cached page */
    uint32_t flags;             /**< Cache entry flags */
    uint32_t ref_count;         /**< Number of references */
    uint32_t access_time;       /**< Last access timestamp */
    uint32_t dirty : 1;         /**< Page has been modified */
    uint32_t locked : 1;        /**< Page is locked */
    uint32_t uptodate : 1;      /**< Page contains valid data */
    struct vmm_page_cache_entry *hash_next;  /**< Hash chain */
    struct vmm_page_cache_entry *lru_next;   /**< LRU list next */
    struct vmm_page_cache_entry *lru_prev;   /**< LRU list prev */
} vmm_page_cache_entry_t;

/**
 * @brief VMM page cache statistics
 */
typedef struct {
    uint32_t hits;              /**< Cache hits */
    uint32_t misses;            /**< Cache misses */
    uint32_t evictions;         /**< Pages evicted */
    uint32_t writebacks;        /**< Dirty pages written back */
    uint32_t total_pages;       /**< Total cached pages */
} vmm_page_cache_stats_t;

// VMM page cache functions
void vmm_page_cache_init(void);
vmm_page_cache_entry_t* vmm_page_cache_lookup(uint32_t inode, uint32_t offset);
vmm_page_cache_entry_t* vmm_page_cache_insert(uint32_t inode, uint32_t offset, uint32_t phys_addr);
void vmm_page_cache_remove(vmm_page_cache_entry_t *entry);
void vmm_page_cache_mark_dirty(vmm_page_cache_entry_t *entry);
void vmm_page_cache_sync(uint32_t inode);
void vmm_page_cache_sync_all(void);
void vmm_page_cache_evict(uint32_t num_pages);
void vmm_page_cache_get_stats(vmm_page_cache_stats_t *stats);
uint32_t vmm_page_cache_shrink(uint32_t target_free);

// ==================== SWAP MANAGEMENT ====================

#define SWAP_MAX_PAGES      (16 * 1024)   /**< Max 64MB swap (16K * 4KB) */
#define SWAP_SIGNATURE      0x53574150    /**< 'SWAP' */

/**
 * @brief Swap page table entry format
 * When a PTE has PRESENT=0 and SWAPPED flag set:
 *   Bits 31-12: Swap slot number
 *   Bits 11-1:  Reserved
 *   Bit 0:      Present (0)
 */
#define PTE_SWAPPED         0x200         /**< Page is in swap (custom flag) */
#define PTE_SWAP_SLOT_SHIFT 12

/**
 * @brief Swap area descriptor
 */
typedef struct {
    uint32_t start_block;       /**< Start block on disk */
    uint32_t total_pages;       /**< Total swap pages available */
    uint32_t free_pages;        /**< Free swap pages */
    uint32_t *bitmap;           /**< Allocation bitmap */
    int      device;            /**< Block device number */
    uint32_t flags;             /**< Swap area flags */
} swap_area_t;

// Swap management functions
void     swap_init(void);
int      swap_out_page(uint32_t virt_addr, uint32_t *pte);
int      swap_in_page(uint32_t virt_addr, uint32_t pte);
uint32_t swap_alloc_slot(void);
void     swap_free_slot(uint32_t slot);
int      swap_add_area(int device, uint32_t start, uint32_t size);
void     swap_get_stats(uint32_t *total, uint32_t *free);

// ==================== DEMAND PAGING ====================

/**
 * @brief Page fault result codes
 */
typedef enum {
    PF_HANDLED = 0,             /**< Fault handled successfully */
    PF_SIGBUS,                  /**< Bad memory access (SIGBUS) */
    PF_SIGSEGV,                 /**< Segmentation violation (SIGSEGV) */
    PF_OOM,                     /**< Out of memory */
    PF_SWAP_ERROR,              /**< Swap I/O error */
} pf_result_t;

// Enhanced page fault handling
pf_result_t handle_page_fault(uint32_t fault_addr, uint32_t error_code);
int demand_page_file(vma_t *vma, uint32_t fault_addr);
int demand_page_anon(vma_t *vma, uint32_t fault_addr);
int handle_cow_fault(uint32_t fault_addr, uint32_t pte);

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
