/**
 * TocinOS Page Cache Implementation
 * 
 * Caches file pages in memory for faster access.
 * Uses a hash table for O(1) lookup and LRU for eviction.
 * 
 * @author TocinOS Team
 */

#include "../../include/kernel/memory.h"
#include "../../include/kernel/kernel.h"

extern void serial_printf(const char *fmt, ...);

/* NULL definition */
#ifndef NULL
#define NULL ((void *)0)
#endif

// ==================== PAGE CACHE DATA STRUCTURES ====================

/* Page cache entry pool */
#define VMM_PAGE_CACHE_POOL_SIZE 512
static vmm_page_cache_entry_t vmm_page_cache_pool[VMM_PAGE_CACHE_POOL_SIZE];
static vmm_page_cache_entry_t *vmm_page_cache_free_list = NULL;

/* Hash table for O(1) lookup */
static vmm_page_cache_entry_t *vmm_page_cache_hash[VMM_PAGE_CACHE_HASH_SIZE];

/* LRU list (doubly linked) */
static vmm_page_cache_entry_t *lru_head = NULL;  /* Most recently used */
static vmm_page_cache_entry_t *lru_tail = NULL;  /* Least recently used */

/* Statistics */
static vmm_page_cache_stats_t cache_stats = {0};

/* Timestamp counter for LRU */
static uint32_t cache_clock = 0;

/* Initialization flag */
static int vmm_vmm_page_cache_initialized = 0;

// ==================== HASH FUNCTIONS ====================

/**
 * Compute hash for (inode, offset) pair
 */
static uint32_t page_cache_hash_fn(uint32_t inode, uint32_t offset) {
    /* Simple but effective hash combining inode and offset */
    uint32_t hash = inode ^ (offset << 4) ^ (offset >> 3);
    return hash & (VMM_PAGE_CACHE_HASH_SIZE - 1);
}

// ==================== LRU MANAGEMENT ====================

/**
 * Move entry to head of LRU (most recently used)
 */
static void lru_touch(vmm_page_cache_entry_t *entry) {
    if (!entry || entry == lru_head) return;
    
    entry->access_time = ++cache_clock;
    
    /* Remove from current position */
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    }
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    }
    if (entry == lru_tail) {
        lru_tail = entry->lru_prev;
    }
    
    /* Insert at head */
    entry->lru_prev = NULL;
    entry->lru_next = lru_head;
    if (lru_head) {
        lru_head->lru_prev = entry;
    }
    lru_head = entry;
    
    if (!lru_tail) {
        lru_tail = entry;
    }
}

/**
 * Add entry to LRU list
 */
static void lru_add(vmm_page_cache_entry_t *entry) {
    entry->access_time = ++cache_clock;
    entry->lru_prev = NULL;
    entry->lru_next = lru_head;
    
    if (lru_head) {
        lru_head->lru_prev = entry;
    }
    lru_head = entry;
    
    if (!lru_tail) {
        lru_tail = entry;
    }
}

/**
 * Remove entry from LRU list
 */
static void lru_remove(vmm_page_cache_entry_t *entry) {
    if (!entry) return;
    
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        lru_head = entry->lru_next;
    }
    
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        lru_tail = entry->lru_prev;
    }
    
    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

// ==================== ENTRY ALLOCATION ====================

/**
 * Allocate a page cache entry
 */
static vmm_page_cache_entry_t* cache_entry_alloc(void) {
    if (!vmm_page_cache_free_list) {
        serial_printf("[PCACHE] No free entries, trying eviction\n");
        vmm_page_cache_evict(4);  /* Try to free some entries */
        
        if (!vmm_page_cache_free_list) {
            serial_printf("[PCACHE] ERROR: No free entries after eviction!\n");
            return NULL;
        }
    }
    
    vmm_page_cache_entry_t *entry = vmm_page_cache_free_list;
    vmm_page_cache_free_list = entry->hash_next;
    
    /* Clear entry */
    entry->inode = 0;
    entry->offset = 0;
    entry->phys_addr = 0;
    entry->flags = 0;
    entry->ref_count = 0;
    entry->access_time = 0;
    entry->dirty = 0;
    entry->locked = 0;
    entry->uptodate = 0;
    entry->hash_next = NULL;
    entry->lru_prev = NULL;
    entry->lru_next = NULL;
    
    return entry;
}

/**
 * Free a page cache entry
 */
static void cache_entry_free(vmm_page_cache_entry_t *entry) {
    if (!entry) return;
    
    entry->hash_next = vmm_page_cache_free_list;
    vmm_page_cache_free_list = entry;
}

// ==================== PAGE CACHE API ====================

/**
 * Initialize the page cache
 */
void vmm_page_cache_init(void) {
    if (vmm_vmm_page_cache_initialized) return;
    
    /* Build free list */
    for (int i = 0; i < VMM_PAGE_CACHE_POOL_SIZE - 1; i++) {
        vmm_page_cache_pool[i].hash_next = &vmm_page_cache_pool[i + 1];
    }
    vmm_page_cache_pool[VMM_PAGE_CACHE_POOL_SIZE - 1].hash_next = NULL;
    vmm_page_cache_free_list = &vmm_page_cache_pool[0];
    
    /* Clear hash table */
    for (int i = 0; i < VMM_PAGE_CACHE_HASH_SIZE; i++) {
        vmm_page_cache_hash[i] = NULL;
    }
    
    /* Clear LRU */
    lru_head = NULL;
    lru_tail = NULL;
    
    /* Clear stats */
    cache_stats.hits = 0;
    cache_stats.misses = 0;
    cache_stats.evictions = 0;
    cache_stats.writebacks = 0;
    cache_stats.total_pages = 0;
    
    vmm_vmm_page_cache_initialized = 1;
    
    serial_printf("[PCACHE] Initialized with %d entries\n", VMM_PAGE_CACHE_POOL_SIZE);
}

/**
 * Look up a page in the cache
 */
vmm_page_cache_entry_t* vmm_page_cache_lookup(uint32_t inode, uint32_t offset) {
    if (!vmm_vmm_page_cache_initialized) vmm_page_cache_init();
    
    uint32_t hash = page_cache_hash_fn(inode, offset);
    vmm_page_cache_entry_t *entry = vmm_page_cache_hash[hash];
    
    while (entry) {
        if (entry->inode == inode && entry->offset == offset) {
            cache_stats.hits++;
            lru_touch(entry);
            return entry;
        }
        entry = entry->hash_next;
    }
    
    cache_stats.misses++;
    return NULL;
}

/**
 * Insert a page into the cache
 */
vmm_page_cache_entry_t* vmm_page_cache_insert(uint32_t inode, uint32_t offset, 
                                       uint32_t phys_addr) {
    if (!vmm_vmm_page_cache_initialized) vmm_page_cache_init();
    
    /* Check if already in cache */
    vmm_page_cache_entry_t *existing = vmm_page_cache_lookup(inode, offset);
    if (existing) {
        serial_printf("[PCACHE] Page already cached: inode=%d off=%d\n", 
                      inode, offset);
        return existing;
    }
    
    /* Allocate new entry */
    vmm_page_cache_entry_t *entry = cache_entry_alloc();
    if (!entry) return NULL;
    
    entry->inode = inode;
    entry->offset = offset;
    entry->phys_addr = phys_addr;
    entry->ref_count = 1;
    entry->uptodate = 1;
    
    /* Add to hash table */
    uint32_t hash = page_cache_hash_fn(inode, offset);
    entry->hash_next = vmm_page_cache_hash[hash];
    vmm_page_cache_hash[hash] = entry;
    
    /* Add to LRU */
    lru_add(entry);
    
    cache_stats.total_pages++;
    
    serial_printf("[PCACHE] Inserted: inode=%d off=%d phys=0x%x\n",
                  inode, offset, phys_addr);
    
    return entry;
}

/**
 * Remove a specific entry from the cache
 */
void vmm_page_cache_remove(vmm_page_cache_entry_t *entry) {
    if (!entry) return;
    
    /* Write back if dirty */
    if (entry->dirty) {
        /* TODO: Implement actual writeback to disk */
        serial_printf("[PCACHE] Writeback: inode=%d off=%d\n",
                      entry->inode, entry->offset);
        cache_stats.writebacks++;
    }
    
    /* Remove from hash table */
    uint32_t hash = page_cache_hash_fn(entry->inode, entry->offset);
    vmm_page_cache_entry_t **pp = &vmm_page_cache_hash[hash];
    
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->hash_next;
            break;
        }
        pp = &(*pp)->hash_next;
    }
    
    /* Remove from LRU */
    lru_remove(entry);
    
    /* Free physical page if no more references */
    if (entry->ref_count <= 1 && entry->phys_addr) {
        pmm_free_page(entry->phys_addr);
    }
    
    cache_stats.total_pages--;
    
    /* Return to free list */
    cache_entry_free(entry);
}

/**
 * Mark a cache entry as dirty
 */
void vmm_page_cache_mark_dirty(vmm_page_cache_entry_t *entry) {
    if (!entry) return;
    
    entry->dirty = 1;
    lru_touch(entry);
}

/**
 * Sync all dirty pages for an inode
 */
void vmm_page_cache_sync(uint32_t inode) {
    if (!vmm_vmm_page_cache_initialized) return;
    
    /* Scan hash table for entries with matching inode */
    for (int i = 0; i < VMM_PAGE_CACHE_HASH_SIZE; i++) {
        vmm_page_cache_entry_t *entry = vmm_page_cache_hash[i];
        
        while (entry) {
            if (entry->inode == inode && entry->dirty) {
                /* TODO: Write page to disk via VFS */
                serial_printf("[PCACHE] Sync: inode=%d off=%d\n",
                              entry->inode, entry->offset);
                entry->dirty = 0;
                cache_stats.writebacks++;
            }
            entry = entry->hash_next;
        }
    }
}

/**
 * Sync all dirty pages in cache
 */
void vmm_page_cache_sync_all(void) {
    if (!vmm_vmm_page_cache_initialized) return;
    
    serial_printf("[PCACHE] Syncing all dirty pages...\n");
    
    for (int i = 0; i < VMM_PAGE_CACHE_HASH_SIZE; i++) {
        vmm_page_cache_entry_t *entry = vmm_page_cache_hash[i];
        
        while (entry) {
            if (entry->dirty) {
                /* TODO: Write page to disk */
                entry->dirty = 0;
                cache_stats.writebacks++;
            }
            entry = entry->hash_next;
        }
    }
}

/**
 * Evict pages from cache (LRU policy)
 */
void vmm_page_cache_evict(uint32_t num_pages) {
    if (!vmm_vmm_page_cache_initialized) return;
    
    uint32_t evicted = 0;
    
    while (evicted < num_pages && lru_tail) {
        vmm_page_cache_entry_t *victim = lru_tail;
        
        /* Skip locked or referenced entries */
        while (victim && (victim->locked || victim->ref_count > 1)) {
            victim = victim->lru_prev;
        }
        
        if (!victim) {
            serial_printf("[PCACHE] No evictable pages\n");
            break;
        }
        
        serial_printf("[PCACHE] Evicting: inode=%d off=%d\n",
                      victim->inode, victim->offset);
        
        vmm_page_cache_remove(victim);
        evicted++;
        cache_stats.evictions++;
    }
    
    serial_printf("[PCACHE] Evicted %d pages\n", evicted);
}

/**
 * Get page cache statistics
 */
void vmm_page_cache_get_stats(vmm_page_cache_stats_t *stats) {
    if (!stats) return;
    
    stats->hits = cache_stats.hits;
    stats->misses = cache_stats.misses;
    stats->evictions = cache_stats.evictions;
    stats->writebacks = cache_stats.writebacks;
    stats->total_pages = cache_stats.total_pages;
}

/**
 * Print page cache statistics
 */
void vmm_page_cache_print_stats(void) {
    uint32_t total = cache_stats.hits + cache_stats.misses;
    uint32_t hit_rate = total ? (cache_stats.hits * 100 / total) : 0;
    
    serial_printf("[PCACHE] Stats: hits=%d misses=%d (hit rate=%d%%)\n",
                  cache_stats.hits, cache_stats.misses, hit_rate);
    serial_printf("[PCACHE] Stats: evictions=%d writebacks=%d total=%d\n",
                  cache_stats.evictions, cache_stats.writebacks, 
                  cache_stats.total_pages);
}

/**
 * Invalidate all cached pages for an inode
 */
void vmm_page_cache_invalidate(uint32_t inode) {
    if (!vmm_vmm_page_cache_initialized) return;
    
    serial_printf("[PCACHE] Invalidating inode %d\n", inode);
    
    for (int i = 0; i < VMM_PAGE_CACHE_HASH_SIZE; i++) {
        vmm_page_cache_entry_t *entry = vmm_page_cache_hash[i];
        vmm_page_cache_entry_t *next;
        
        while (entry) {
            next = entry->hash_next;
            
            if (entry->inode == inode) {
                vmm_page_cache_remove(entry);
            }
            
            entry = next;
        }
    }
}

/**
 * Try to shrink the page cache to free memory
 */
uint32_t vmm_page_cache_shrink(uint32_t target_free) {
    uint32_t freed = 0;
    
    while (freed < target_free && cache_stats.total_pages > 0) {
        vmm_page_cache_entry_t *victim = lru_tail;
        
        /* Find non-locked, non-dirty candidate */
        while (victim && (victim->locked || victim->ref_count > 1 || 
                          victim->dirty)) {
            victim = victim->lru_prev;
        }
        
        if (!victim) break;
        
        vmm_page_cache_remove(victim);
        freed++;
    }
    
    return freed;
}
