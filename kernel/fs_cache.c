/**
 * TocinOS File System Cache Implementation
 * 
 * High-performance caching for file system I/O
 */

#include "../include/kernel/fs_cache.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/timer.h"
#include "../include/kernel/kernel.h"

// ==================== PAGE CACHE ====================

static page_cache_t page_cache;
static page_cache_entry_t page_entries[PAGE_CACHE_SIZE];
static int page_cache_initialized = 0;

/**
 * Hash function for page cache
 */
static uint32_t page_cache_hash(uint32_t file_id, uint32_t page_offset) {
    return ((file_id << 16) ^ page_offset) % PAGE_CACHE_HASH_SIZE;
}

/**
 * Remove entry from LRU list
 */
static void page_cache_lru_remove(page_cache_entry_t *entry) {
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        page_cache.lru_head = entry->lru_next;
    }
    
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        page_cache.lru_tail = entry->lru_prev;
    }
    
    entry->lru_prev = 0;
    entry->lru_next = 0;
}

/**
 * Add entry to LRU head (most recently used)
 */
static void page_cache_lru_add(page_cache_entry_t *entry) {
    entry->lru_next = page_cache.lru_head;
    entry->lru_prev = 0;
    
    if (page_cache.lru_head) {
        page_cache.lru_head->lru_prev = entry;
    } else {
        page_cache.lru_tail = entry;
    }
    
    page_cache.lru_head = entry;
}

/**
 * Find page in cache
 */
static page_cache_entry_t* page_cache_find(uint32_t file_id, uint32_t page_offset) {
    uint32_t hash = page_cache_hash(file_id, page_offset);
    page_cache_entry_t *entry = page_cache.hash_table[hash];
    
    while (entry) {
        if (entry->file_id == file_id && entry->page_offset == page_offset) {
            // Move to LRU head
            if (entry != page_cache.lru_head) {
                page_cache_lru_remove(entry);
                page_cache_lru_add(entry);
            }
            entry->flags |= PAGECACHE_REFERENCED;
            entry->timestamp = timer_get_ticks();
            page_cache.hits++;
            return entry;
        }
        entry = entry->next;
    }
    
    page_cache.misses++;
    return 0;
}

/**
 * Initialize page cache
 */
void page_cache_init(void) {
    if (page_cache_initialized) {
        return;
    }
    
    // Clear hash table
    for (int i = 0; i < PAGE_CACHE_HASH_SIZE; i++) {
        page_cache.hash_table[i] = 0;
    }
    
    page_cache.lru_head = 0;
    page_cache.lru_tail = 0;
    page_cache.num_pages = 0;
    page_cache.num_dirty = 0;
    page_cache.hits = 0;
    page_cache.misses = 0;
    
    // Initialize page entries
    for (int i = 0; i < PAGE_CACHE_SIZE; i++) {
        page_entries[i].file_id = 0;
        page_entries[i].page_offset = 0;
        page_entries[i].data = 0;
        page_entries[i].flags = 0;
        page_entries[i].timestamp = 0;
        page_entries[i].next = 0;
        page_entries[i].lru_prev = 0;
        page_entries[i].lru_next = 0;
    }
    
    page_cache_initialized = 1;
    kernel_print("[CACHE] Page cache initialized (4MB capacity)\n");
}

/**
 * Evict LRU page
 */
page_cache_entry_t* page_cache_evict_lru(void) {
    // Start from tail (least recently used)
    page_cache_entry_t *entry = page_cache.lru_tail;
    
    while (entry) {
        // Skip locked and dirty pages
        if (!(entry->flags & PAGECACHE_LOCKED) && !(entry->flags & PAGECACHE_DIRTY)) {
            // Remove from hash table
            uint32_t hash = page_cache_hash(entry->file_id, entry->page_offset);
            page_cache_entry_t *curr = page_cache.hash_table[hash];
            page_cache_entry_t *prev = 0;
            
            while (curr) {
                if (curr == entry) {
                    if (prev) {
                        prev->next = curr->next;
                    } else {
                        page_cache.hash_table[hash] = curr->next;
                    }
                    break;
                }
                prev = curr;
                curr = curr->next;
            }
            
            // Remove from LRU
            page_cache_lru_remove(entry);
            
            // Free page data
            if (entry->data) {
                kfree(entry->data);
                entry->data = 0;
            }
            
            page_cache.num_pages--;
            return entry;
        }
        
        entry = entry->lru_prev;
    }
    
    return 0;
}

/**
 * Read page from cache
 */
void* page_cache_read(uint32_t file_id, uint32_t page_offset) {
    if (!page_cache_initialized) {
        return 0;
    }
    
    page_cache_entry_t *entry = page_cache_find(file_id, page_offset);
    if (entry && (entry->flags & PAGECACHE_UPTODATE)) {
        return entry->data;
    }
    
    // Cache miss - need to read from disk
    // For now, return NULL (caller must handle disk I/O)
    return 0;
}

/**
 * Write page to cache
 */
int page_cache_write(uint32_t file_id, uint32_t page_offset, const void *data) {
    if (!page_cache_initialized || !data) {
        return -1;
    }
    
    page_cache_entry_t *entry = page_cache_find(file_id, page_offset);
    
    if (!entry) {
        // Need to allocate new entry
        if (page_cache.num_pages >= PAGE_CACHE_SIZE) {
            // Evict LRU page
            entry = page_cache_evict_lru();
            if (!entry) {
                return -1;  // Cannot evict
            }
        } else {
            // Use next available entry
            for (int i = 0; i < PAGE_CACHE_SIZE; i++) {
                if (!page_entries[i].data) {
                    entry = &page_entries[i];
                    break;
                }
            }
        }
        
        if (!entry) {
            return -1;
        }
        
        // Allocate page data
        entry->data = kmalloc(PAGE_SIZE);
        if (!entry->data) {
            return -1;
        }
        
        // Initialize entry
        entry->file_id = file_id;
        entry->page_offset = page_offset;
        entry->flags = 0;
        
        // Add to hash table
        uint32_t hash = page_cache_hash(file_id, page_offset);
        entry->next = page_cache.hash_table[hash];
        page_cache.hash_table[hash] = entry;
        
        // Add to LRU
        page_cache_lru_add(entry);
        page_cache.num_pages++;
    }
    
    // Copy data
    uint8_t *src = (uint8_t *)data;
    uint8_t *dest = (uint8_t *)entry->data;
    for (int i = 0; i < PAGE_SIZE; i++) {
        dest[i] = src[i];
    }
    
    // Mark as dirty and up-to-date
    if (!(entry->flags & PAGECACHE_DIRTY)) {
        entry->flags |= PAGECACHE_DIRTY;
        page_cache.num_dirty++;
    }
    entry->flags |= PAGECACHE_UPTODATE;
    entry->timestamp = timer_get_ticks();
    
    return 0;
}

/**
 * Flush dirty page to disk
 */
int page_cache_flush(uint32_t file_id, uint32_t page_offset) {
    if (!page_cache_initialized) {
        return -1;
    }
    
    page_cache_entry_t *entry = page_cache_find(file_id, page_offset);
    if (!entry || !(entry->flags & PAGECACHE_DIRTY)) {
        return 0;  // Nothing to flush
    }
    
    // Lock page
    entry->flags |= PAGECACHE_LOCKED | PAGECACHE_WRITEBACK;
    
    // TODO: Write page to disk through VFS/filesystem
    // For now, just mark as clean
    
    entry->flags &= ~(PAGECACHE_DIRTY | PAGECACHE_LOCKED | PAGECACHE_WRITEBACK);
    page_cache.num_dirty--;
    
    return 0;
}

/**
 * Flush all pages for a file
 */
int page_cache_flush_all(uint32_t file_id) {
    int flushed = 0;
    
    for (int i = 0; i < PAGE_CACHE_SIZE; i++) {
        if (page_entries[i].data && page_entries[i].file_id == file_id &&
            (page_entries[i].flags & PAGECACHE_DIRTY)) {
            if (page_cache_flush(file_id, page_entries[i].page_offset) == 0) {
                flushed++;
            }
        }
    }
    
    return flushed;
}

/**
 * Invalidate page
 */
int page_cache_invalidate(uint32_t file_id, uint32_t page_offset) {
    page_cache_entry_t *entry = page_cache_find(file_id, page_offset);
    if (!entry) {
        return 0;
    }
    
    // Remove from hash table
    uint32_t hash = page_cache_hash(file_id, page_offset);
    page_cache_entry_t *curr = page_cache.hash_table[hash];
    page_cache_entry_t *prev = 0;
    
    while (curr) {
        if (curr == entry) {
            if (prev) {
                prev->next = curr->next;
            } else {
                page_cache.hash_table[hash] = curr->next;
            }
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    
    // Remove from LRU
    page_cache_lru_remove(entry);
    
    // Update counts
    if (entry->flags & PAGECACHE_DIRTY) {
        page_cache.num_dirty--;
    }
    page_cache.num_pages--;
    
    // Free data
    if (entry->data) {
        kfree(entry->data);
        entry->data = 0;
    }
    
    return 0;
}

/**
 * Get page cache statistics
 */
void page_cache_get_stats(uint32_t *num_pages, uint32_t *num_dirty,
                          uint64_t *hits, uint64_t *misses) {
    if (num_pages) *num_pages = page_cache.num_pages;
    if (num_dirty) *num_dirty = page_cache.num_dirty;
    if (hits) *hits = page_cache.hits;
    if (misses) *misses = page_cache.misses;
}

// ==================== UNIFIED CACHE INTERFACE ====================

/**
 * Initialize all caches
 */
void cache_init(void) {
    page_cache_init();
    // buffer_cache_init();  // TODO
    // writeback_init();     // TODO
    kernel_print("[CACHE] File system cache system initialized\n");
}

/**
 * Read page through cache
 */
void* cache_read_page(uint32_t file_id, uint32_t offset) {
    uint32_t page_offset = offset / PAGE_SIZE;
    return page_cache_read(file_id, page_offset);
}

/**
 * Write page through cache
 */
int cache_write_page(uint32_t file_id, uint32_t offset, const void *data) {
    uint32_t page_offset = offset / PAGE_SIZE;
    return page_cache_write(file_id, page_offset, data);
}

/**
 * Sync all dirty pages for file
 */
int cache_sync_file(uint32_t file_id) {
    return page_cache_flush_all(file_id);
}

/**
 * Drop all pages for file
 */
int cache_drop_file(uint32_t file_id) {
    int dropped = 0;
    
    for (int i = 0; i < PAGE_CACHE_SIZE; i++) {
        if (page_entries[i].data && page_entries[i].file_id == file_id) {
            page_cache_invalidate(file_id, page_entries[i].page_offset);
            dropped++;
        }
    }
    
    return dropped;
}

/**
 * Get cache statistics
 */
void cache_get_stats(cache_stats_t *stats) {
    if (!stats) {
        return;
    }
    
    page_cache_get_stats(&stats->page_cache_size, &stats->page_cache_dirty,
                         &stats->page_cache_hits, &stats->page_cache_misses);
    
    // TODO: Buffer cache stats
    stats->buffer_cache_hits = 0;
    stats->buffer_cache_misses = 0;
    stats->buffer_cache_size = 0;
    stats->buffer_cache_dirty = 0;
    
    stats->writeback_count = 0;
    stats->pages_written = 0;
    stats->eviction_count = 0;
}

/**
 * Print cache statistics
 */
void cache_print_stats(void) {
    cache_stats_t stats;
    cache_get_stats(&stats);
    
    kernel_print("\n=== File System Cache Statistics ===\n");
    kernel_print("Page Cache:\n");
    kernel_print("  Cached pages: ");
    // TODO: Print numbers
    kernel_print("\n  Dirty pages: ");
    kernel_print("\n  Cache hits: ");
    kernel_print("\n  Cache misses: ");
    
    // Calculate hit rate using 32-bit arithmetic
    uint32_t hits = (uint32_t)(stats.page_cache_hits & 0xFFFFFFFF);
    uint32_t misses = (uint32_t)(stats.page_cache_misses & 0xFFFFFFFF);
    uint32_t total = hits + misses;
    if (total > 0) {
        uint32_t hit_rate = (hits * 100) / total;
        kernel_print("\n  Hit rate: ");
        // TODO: Print hit_rate
        kernel_print("%\n");
    }
    kernel_print("\n");
}
