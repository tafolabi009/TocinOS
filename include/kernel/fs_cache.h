/**
 * TocinOS File System Cache
 * 
 * Implements page cache and buffer cache for improved I/O performance.
 * Features:
 * - Page cache for file data
 * - Buffer cache for block devices
 * - Write-back caching
 * - LRU eviction policy
 * - Read-ahead support
 * - Dirty page tracking
 */

#ifndef FS_CACHE_H
#define FS_CACHE_H

#include <stdint.h>

// ==================== PAGE CACHE ====================

#define PAGE_CACHE_SIZE 1024      // Max cached pages
#define PAGE_CACHE_HASH_SIZE 256  // Hash table size

// Page cache entry flags
#define PAGECACHE_DIRTY    0x01   // Page has been modified
#define PAGECACHE_LOCKED   0x02   // Page is locked for I/O
#define PAGECACHE_UPTODATE 0x04   // Page data is valid
#define PAGECACHE_REFERENCED 0x08 // Recently accessed
#define PAGECACHE_WRITEBACK 0x10  // Currently being written back

typedef struct page_cache_entry {
    uint32_t file_id;             // File identifier (inode number)
    uint32_t page_offset;         // Page offset in file
    void *data;                   // Page data (4KB)
    uint32_t flags;               // Cache flags
    uint64_t timestamp;           // Last access time
    struct page_cache_entry *next; // Hash collision chain
    struct page_cache_entry *lru_prev;
    struct page_cache_entry *lru_next;
} page_cache_entry_t;

typedef struct {
    page_cache_entry_t *hash_table[PAGE_CACHE_HASH_SIZE];
    page_cache_entry_t *lru_head;
    page_cache_entry_t *lru_tail;
    uint32_t num_pages;
    uint32_t num_dirty;
    uint64_t hits;
    uint64_t misses;
} page_cache_t;

// Page cache operations
void page_cache_init(void);
void* page_cache_read(uint32_t file_id, uint32_t page_offset);
int page_cache_write(uint32_t file_id, uint32_t page_offset, const void *data);
int page_cache_flush(uint32_t file_id, uint32_t page_offset);
int page_cache_flush_all(uint32_t file_id);
int page_cache_invalidate(uint32_t file_id, uint32_t page_offset);
int page_cache_invalidate_all(uint32_t file_id);

// Advanced page cache operations
void page_cache_mark_dirty(uint32_t file_id, uint32_t page_offset);
int page_cache_is_uptodate(uint32_t file_id, uint32_t page_offset);
void page_cache_lock(uint32_t file_id, uint32_t page_offset);
void page_cache_unlock(uint32_t file_id, uint32_t page_offset);
int page_cache_wait_on_page(uint32_t file_id, uint32_t page_offset);

// Read-ahead
int page_cache_readahead(uint32_t file_id, uint32_t start_offset, uint32_t num_pages);

// Statistics
void page_cache_get_stats(uint32_t *num_pages, uint32_t *num_dirty, 
                          uint64_t *hits, uint64_t *misses);

// ==================== BUFFER CACHE ====================

#define BUFFER_CACHE_SIZE 512     // Max cached buffers
#define BUFFER_SIZE 4096          // Buffer size (block size)

// Buffer cache entry flags
#define BUFFER_DIRTY    0x01      // Buffer has been modified
#define BUFFER_LOCKED   0x02      // Buffer is locked
#define BUFFER_UPTODATE 0x04      // Buffer data is valid
#define BUFFER_MAPPED   0x08      // Buffer is mapped to disk

typedef struct buffer_head {
    uint32_t device_id;           // Device identifier
    uint32_t block_num;           // Block number on device
    void *data;                   // Buffer data
    uint32_t size;                // Buffer size
    uint32_t flags;               // Buffer flags
    uint64_t timestamp;           // Last access time
    struct buffer_head *next;     // Hash collision chain
    struct buffer_head *lru_prev;
    struct buffer_head *lru_next;
} buffer_head_t;

typedef struct {
    buffer_head_t *hash_table[PAGE_CACHE_HASH_SIZE];
    buffer_head_t *lru_head;
    buffer_head_t *lru_tail;
    uint32_t num_buffers;
    uint32_t num_dirty;
    uint64_t hits;
    uint64_t misses;
} buffer_cache_t;

// Buffer cache operations
void buffer_cache_init(void);
buffer_head_t* buffer_cache_get(uint32_t device_id, uint32_t block_num);
buffer_head_t* buffer_cache_read(uint32_t device_id, uint32_t block_num);
int buffer_cache_write(uint32_t device_id, uint32_t block_num, const void *data);
int buffer_cache_sync(uint32_t device_id, uint32_t block_num);
int buffer_cache_sync_all(uint32_t device_id);
int buffer_cache_invalidate(uint32_t device_id, uint32_t block_num);
int buffer_cache_invalidate_all(uint32_t device_id);

// Advanced buffer operations
void buffer_mark_dirty(buffer_head_t *bh);
void buffer_lock(buffer_head_t *bh);
void buffer_unlock(buffer_head_t *bh);
int buffer_wait_on_buffer(buffer_head_t *bh);

// ==================== WRITEBACK DAEMON ====================

typedef struct {
    uint32_t interval_ms;         // Writeback interval in milliseconds
    uint32_t dirty_threshold;     // Start writeback at this % dirty
    uint32_t dirty_background_threshold;
    int enabled;
    uint64_t total_writebacks;
    uint64_t pages_written;
} writeback_control_t;

// Writeback operations
void writeback_init(void);
void writeback_start(void);
void writeback_stop(void);
void writeback_daemon(void);    // Called periodically by timer
int writeback_dirty_pages(uint32_t max_pages);
int writeback_file(uint32_t file_id);

// ==================== CACHE EVICTION ====================

// LRU eviction policy
page_cache_entry_t* page_cache_evict_lru(void);
buffer_head_t* buffer_cache_evict_lru(void);

// Clock algorithm (approximate LRU)
page_cache_entry_t* page_cache_evict_clock(void);
buffer_head_t* buffer_cache_evict_clock(void);

// Eviction control
void cache_set_min_free_pages(uint32_t num_pages);
void cache_set_max_dirty_ratio(uint32_t ratio);
int cache_shrink(uint32_t num_pages);
int cache_drop_all(void);

// ==================== PREFETCHING ====================

typedef struct {
    uint32_t file_id;
    uint32_t start_offset;
    uint32_t end_offset;
    uint32_t current_offset;
    int active;
} prefetch_request_t;

#define MAX_PREFETCH_REQUESTS 16

// Prefetch operations
int prefetch_start(uint32_t file_id, uint32_t start_offset, uint32_t size);
int prefetch_stop(uint32_t file_id);
void prefetch_daemon(void);

// ==================== UNIFIED CACHE INTERFACE ====================

typedef enum {
    CACHE_READ = 0,
    CACHE_WRITE = 1,
    CACHE_READAHEAD = 2
} cache_op_t;

// Unified cache operations
void cache_init(void);
void* cache_read_page(uint32_t file_id, uint32_t offset);
int cache_write_page(uint32_t file_id, uint32_t offset, const void *data);
int cache_sync_file(uint32_t file_id);
int cache_sync_all(void);
int cache_drop_file(uint32_t file_id);

// ==================== CACHE STATISTICS ====================

typedef struct {
    uint64_t page_cache_hits;
    uint64_t page_cache_misses;
    uint64_t buffer_cache_hits;
    uint64_t buffer_cache_misses;
    uint32_t page_cache_size;
    uint32_t page_cache_dirty;
    uint32_t buffer_cache_size;
    uint32_t buffer_cache_dirty;
    uint64_t writeback_count;
    uint64_t pages_written;
    uint64_t eviction_count;
} cache_stats_t;

void cache_get_stats(cache_stats_t *stats);
void cache_print_stats(void);
void cache_reset_stats(void);

// ==================== MEMORY PRESSURE ====================

typedef enum {
    PRESSURE_LOW = 0,
    PRESSURE_MEDIUM = 1,
    PRESSURE_HIGH = 2,
    PRESSURE_CRITICAL = 3
} memory_pressure_t;

memory_pressure_t cache_get_memory_pressure(void);
int cache_handle_memory_pressure(memory_pressure_t pressure);

// ==================== CACHE CONFIGURATION ====================

typedef struct {
    uint32_t page_cache_max_size;       // Max pages in cache
    uint32_t buffer_cache_max_size;     // Max buffers in cache
    uint32_t dirty_writeback_interval;  // ms between writebacks
    uint32_t dirty_ratio;               // Max % dirty before sync writeback
    uint32_t dirty_background_ratio;    // Start background writeback at %
    uint32_t readahead_kb;              // Read-ahead size in KB
    int writeback_enabled;
    int readahead_enabled;
} cache_config_t;

void cache_set_config(const cache_config_t *config);
void cache_get_config(cache_config_t *config);

#endif // FS_CACHE_H
