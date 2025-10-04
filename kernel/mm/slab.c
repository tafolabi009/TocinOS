/**
 * TocinOS Slab Allocator Implementation
 * 
 * Efficient allocation for kernel objects.
 * Based on the Solaris slab allocator design.
 */

#include "../include/kernel/memory.h"
#include "../include/kernel/kernel.h"

#define SLAB_SIZE PAGE_SIZE
#define SLAB_MAGIC 0x5LAB5LAB

// General purpose caches for common sizes
static kmem_cache_t *size_caches[10];  // 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
static kmem_cache_t all_caches[MAX_SLABS];
static int num_caches = 0;
static int slab_initialized = 0;

/**
 * Helper: String copy
 */
static void str_copy(char *dest, const char *src, int max_len) {
    int i = 0;
    while (src && src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/**
 * Helper: Align size
 */
static uint32_t align_size(uint32_t size, uint32_t align) {
    if (align == 0) {
        align = sizeof(void *);
    }
    return (size + align - 1) & ~(align - 1);
}

/**
 * Helper: Allocate a new slab
 */
static slab_t* alloc_slab(kmem_cache_t *cache) {
    // Allocate page for slab
    void *slab_mem = buddy_alloc(0);  // 1 page
    if (!slab_mem) {
        return 0;
    }
    
    slab_t *slab = (slab_t *)slab_mem;
    slab->next = 0;
    slab->inuse = 0;
    
    // Calculate number of objects that fit in this slab
    uint32_t available = SLAB_SIZE - sizeof(slab_t);
    slab->total = available / cache->obj_size;
    
    // Initialize free list
    uint8_t *obj_start = (uint8_t *)slab_mem + sizeof(slab_t);
    slab->free_list = (void *)obj_start;
    
    // Link free objects
    for (uint32_t i = 0; i < slab->total - 1; i++) {
        void **obj = (void **)(obj_start + (i * cache->obj_size));
        *obj = (void *)(obj_start + ((i + 1) * cache->obj_size));
    }
    
    // Last object points to NULL
    void **last_obj = (void **)(obj_start + ((slab->total - 1) * cache->obj_size));
    *last_obj = 0;
    
    cache->num_slabs++;
    
    return slab;
}

/**
 * Helper: Free a slab
 */
static void free_slab(kmem_cache_t *cache, slab_t *slab) {
    if (!slab) {
        return;
    }
    
    // Call destructor for all objects if provided
    if (cache->dtor) {
        uint8_t *obj_start = (uint8_t *)slab + sizeof(slab_t);
        for (uint32_t i = 0; i < slab->total; i++) {
            void *obj = (void *)(obj_start + (i * cache->obj_size));
            cache->dtor(obj);
        }
    }
    
    buddy_free(slab, 0);
    cache->num_slabs--;
}

/**
 * Initialize slab allocator
 */
void slab_init(void) {
    if (slab_initialized) {
        return;
    }
    
    // Clear cache array
    for (int i = 0; i < MAX_SLABS; i++) {
        all_caches[i].name[0] = '\0';
        all_caches[i].obj_size = 0;
    }
    
    // Create general purpose size caches
    uint32_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    for (int i = 0; i < 10; i++) {
        char name[32];
        name[0] = 's'; name[1] = 'i'; name[2] = 'z'; name[3] = 'e'; name[4] = '-';
        // Simple integer to string
        uint32_t size = sizes[i];
        int pos = 5;
        if (size >= 1000) {
            name[pos++] = '0' + (size / 1000);
            size %= 1000;
        }
        if (size >= 100 || sizes[i] >= 1000) {
            name[pos++] = '0' + (size / 100);
            size %= 100;
        }
        if (size >= 10 || sizes[i] >= 100) {
            name[pos++] = '0' + (size / 10);
            size %= 10;
        }
        name[pos++] = '0' + size;
        name[pos] = '\0';
        
        size_caches[i] = kmem_cache_create(name, sizes[i], 0, 0, 0, 0);
    }
    
    slab_initialized = 1;
    kernel_print("[SLAB] Slab allocator initialized\n");
}

/**
 * Create a new memory cache
 */
kmem_cache_t* kmem_cache_create(const char *name, uint32_t size, uint32_t align,
                                uint32_t flags, void (*ctor)(void *), void (*dtor)(void *)) {
    if (!name || size == 0 || num_caches >= MAX_SLABS) {
        return 0;
    }
    
    // Find free cache slot
    kmem_cache_t *cache = 0;
    for (int i = 0; i < MAX_SLABS; i++) {
        if (all_caches[i].name[0] == '\0') {
            cache = &all_caches[i];
            break;
        }
    }
    
    if (!cache) {
        return 0;
    }
    
    // Initialize cache
    str_copy(cache->name, name, SLAB_NAME_LEN);
    cache->obj_size = align_size(size, align);
    cache->align = align ? align : sizeof(void *);
    cache->flags = flags;
    cache->slabs_full = 0;
    cache->slabs_partial = 0;
    cache->slabs_free = 0;
    cache->num_slabs = 0;
    cache->num_objs = 0;
    cache->num_active = 0;
    cache->ctor = ctor;
    cache->dtor = dtor;
    
    num_caches++;
    
    return cache;
}

/**
 * Destroy a memory cache
 */
void kmem_cache_destroy(kmem_cache_t *cache) {
    if (!cache) {
        return;
    }
    
    // Free all slabs
    while (cache->slabs_full) {
        slab_t *next = cache->slabs_full->next;
        free_slab(cache, cache->slabs_full);
        cache->slabs_full = next;
    }
    
    while (cache->slabs_partial) {
        slab_t *next = cache->slabs_partial->next;
        free_slab(cache, cache->slabs_partial);
        cache->slabs_partial = next;
    }
    
    while (cache->slabs_free) {
        slab_t *next = cache->slabs_free->next;
        free_slab(cache, cache->slabs_free);
        cache->slabs_free = next;
    }
    
    cache->name[0] = '\0';
    num_caches--;
}

/**
 * Allocate an object from cache
 */
void* kmem_cache_alloc(kmem_cache_t *cache) {
    if (!cache) {
        return 0;
    }
    
    slab_t *slab = 0;
    
    // Try to get from partial slab first
    if (cache->slabs_partial) {
        slab = cache->slabs_partial;
    }
    // Otherwise try free slab
    else if (cache->slabs_free) {
        slab = cache->slabs_free;
        cache->slabs_free = slab->next;
        slab->next = cache->slabs_partial;
        cache->slabs_partial = slab;
    }
    // Need to allocate new slab
    else {
        slab = alloc_slab(cache);
        if (!slab) {
            return 0;  // Out of memory
        }
        slab->next = cache->slabs_partial;
        cache->slabs_partial = slab;
    }
    
    // Get object from free list
    void *obj = slab->free_list;
    if (!obj) {
        return 0;  // Should not happen
    }
    
    slab->free_list = *(void **)obj;
    slab->inuse++;
    cache->num_active++;
    
    // Move to full list if slab is now full
    if (slab->inuse == slab->total) {
        // Remove from partial list
        if (cache->slabs_partial == slab) {
            cache->slabs_partial = slab->next;
        } else {
            slab_t *prev = cache->slabs_partial;
            while (prev && prev->next != slab) {
                prev = prev->next;
            }
            if (prev) {
                prev->next = slab->next;
            }
        }
        
        // Add to full list
        slab->next = cache->slabs_full;
        cache->slabs_full = slab;
    }
    
    // Call constructor if provided
    if (cache->ctor) {
        cache->ctor(obj);
    }
    
    return obj;
}

/**
 * Free an object back to cache
 */
void kmem_cache_free(kmem_cache_t *cache, void *obj) {
    if (!cache || !obj) {
        return;
    }
    
    // Call destructor if provided
    if (cache->dtor) {
        cache->dtor(obj);
    }
    
    // Find which slab this object belongs to
    // (simplified - assumes object is in a slab we manage)
    uint32_t obj_addr = (uint32_t)obj;
    uint32_t slab_addr = obj_addr & ~(SLAB_SIZE - 1);
    slab_t *slab = (slab_t *)slab_addr;
    
    // Return object to free list
    *(void **)obj = slab->free_list;
    slab->free_list = obj;
    slab->inuse--;
    cache->num_active--;
    
    // Move slab between lists if necessary
    if (slab->inuse == 0) {
        // Slab is now empty - consider freeing it or moving to free list
        // For now, move to free list
    }
}

/**
 * General purpose kernel memory allocation
 */
void* kmalloc(uint32_t size) {
    if (!slab_initialized || size == 0) {
        return 0;
    }
    
    // Find appropriate size cache
    for (int i = 0; i < 10; i++) {
        if (size_caches[i] && size <= size_caches[i]->obj_size) {
            return kmem_cache_alloc(size_caches[i]);
        }
    }
    
    // Size too large, use buddy allocator directly
    uint32_t order = buddy_get_order(size);
    return buddy_alloc(order);
}

/**
 * Allocate and zero kernel memory
 */
void* kzalloc(uint32_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        // Zero out memory
        uint8_t *p = (uint8_t *)ptr;
        for (uint32_t i = 0; i < size; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

/**
 * Free kernel memory
 */
void kfree(void *ptr) {
    if (!ptr) {
        return;
    }
    
    // Try to determine which cache this belongs to
    // (simplified implementation - real system would track allocations)
    // For now, assume it's from a size cache or buddy allocator
    
    // This is a simplified version - a real implementation would need
    // metadata to track which cache/allocator was used
}
