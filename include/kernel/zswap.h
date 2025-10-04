/**
 * TocinOS zswap - Memory Compression
 * 
 * Compressed swap cache implementation
 */

#ifndef ZSWAP_H
#define ZSWAP_H

#include "../stdint.h"

// Compression algorithms
enum zswap_compressor {
    ZSWAP_COMP_LZ4,      // Fast, moderate compression
    ZSWAP_COMP_ZSTD,     // Better compression, slower
    ZSWAP_COMP_LZO,      // Very fast, lower compression
};

// Red-black tree node for zswap entries
struct zswap_rb_node {
    struct zswap_rb_node *left;
    struct zswap_rb_node *right;
    struct zswap_rb_node *parent;
    int color;
};

// zswap entry
typedef struct zswap_entry {
    struct zswap_rb_node rb_node;   // RB-tree node
    uint64_t offset;                // Offset in pool
    uint32_t compressed_size;       // Size after compression
    uint16_t swapfile_id;          // Swap file ID
} zswap_entry_t;

// zswap pool
typedef struct zswap_pool {
    struct {
        struct zswap_rb_node *node;
    } rb_root;                      // Red-black tree of compressed pages
    int lock;                       // Pool lock (spinlock)
    uint64_t pages_stored;          // Number of compressed pages
    uint64_t total_compressed_size; // Total compressed size
    uint64_t total_uncompressed_size; // Original size
} zswap_pool_t;

// Function prototypes

/**
 * Initialize zswap
 */
void zswap_init(void);

/**
 * Compress a page
 */
int zswap_compress_page(const void *src, void *dst, uint32_t *dst_len);

/**
 * Decompress a page
 */
int zswap_decompress_page(const void *src, uint32_t src_len, void *dst);

/**
 * Store page in zswap
 */
int zswap_store_page(uint64_t offset, const void *page);

/**
 * Load page from zswap
 */
int zswap_load_page(uint64_t offset, void *page);

/**
 * Get zswap statistics
 */
void zswap_get_stats(uint64_t *stored, uint64_t *compressed_size, uint64_t *uncompressed_size);

#endif // ZSWAP_H
