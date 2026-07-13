/**
 * TocinOS Slab Allocator + kmalloc/kfree
 *
 * Object caches on top of the buddy allocator (see memory.h for the full
 * kmalloc routing contract). Every slab is one buddy order-0 page:
 *
 *   +-----------------+--------------------------------------------+
 *   | slab_t header   | objects ... (obj_size each)                |
 *   | magic, cache,   |                                            |
 *   | links, freelist |                                            |
 *   +-----------------+--------------------------------------------+
 *   page base         page base + SLAB_OBJ_OFFSET
 *
 * The in-page header lets kfree() recover the owning cache from a bare
 * pointer: page-aligned pointers are buddy-direct large allocations,
 * anything else masks down to the page base and validates SLAB_MAGIC.
 *
 * This file replaces an earlier non-functional AI-generated draft whose
 * kfree() was an empty stub (every allocation leaked), whose 4096-byte
 * size class underflowed its object count and scribbled the whole
 * address space, and whose full slabs never returned to the partial
 * list. See tests/unit/test_slab.c for the behavioral contract.
 */

#include "../include/kernel/memory.h"

/* Objects start at the first 16-byte boundary after the header. */
#define SLAB_OBJ_OFFSET (((uint32_t)sizeof(slab_t) + 15u) & ~15u)

/* Which cache list a slab currently sits on (slab_t.list_id). */
enum {
    SLAB_LIST_NONE = 0,
    SLAB_LIST_FULL,
    SLAB_LIST_PARTIAL,
    SLAB_LIST_FREE,
};

/* kmalloc size classes; larger requests go straight to the buddy. */
#define KMALLOC_NUM_CLASSES 8
static const uint32_t kmalloc_class_size[KMALLOC_NUM_CLASSES] = {
    8, 16, 32, 64, 128, 256, 512, 1024
};
static kmem_cache_t *size_caches[KMALLOC_NUM_CLASSES];

static kmem_cache_t all_caches[MAX_SLABS];
static int num_caches;
static int slab_initialized;

/* ---- small freestanding helpers ----------------------------------------- */

static void str_copy(char *dest, const char *src, int max_len) {
    int i = 0;
    while (src && src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/** Append decimal value to dest at pos; returns new pos. */
static int append_u32(char *dest, int pos, uint32_t value) {
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    while (n > 0) {
        dest[pos++] = tmp[--n];
    }
    dest[pos] = '\0';
    return pos;
}

static uint32_t align_size(uint32_t size, uint32_t align) {
    if (align == 0) {
        align = (uint32_t)sizeof(void *);
    }
    return (size + align - 1) & ~(align - 1);
}

/* ---- slab list management ------------------------------------------------ */

static slab_t** list_head_of(kmem_cache_t *cache, uint32_t list_id) {
    switch (list_id) {
        case SLAB_LIST_FULL:    return &cache->slabs_full;
        case SLAB_LIST_PARTIAL: return &cache->slabs_partial;
        case SLAB_LIST_FREE:    return &cache->slabs_free;
        default:                return 0;
    }
}

static void slab_list_remove(kmem_cache_t *cache, slab_t *slab) {
    slab_t **head = list_head_of(cache, slab->list_id);
    if (!head) {
        return;
    }
    if (slab->prev) {
        slab->prev->next = slab->next;
    } else {
        *head = slab->next;
    }
    if (slab->next) {
        slab->next->prev = slab->prev;
    }
    slab->next = 0;
    slab->prev = 0;
    slab->list_id = SLAB_LIST_NONE;
}

static void slab_list_push(kmem_cache_t *cache, slab_t *slab, uint32_t list_id) {
    slab_t **head = list_head_of(cache, list_id);
    slab->prev = 0;
    slab->next = *head;
    if (*head) {
        (*head)->prev = slab;
    }
    *head = slab;
    slab->list_id = list_id;
}

/* ---- slab page lifecycle -------------------------------------------------- */

static slab_t* alloc_slab(kmem_cache_t *cache) {
    void *page = buddy_alloc(0);  /* one page from the high region */
    if (!page) {
        return 0;
    }

    slab_t *slab = (slab_t *)page;
    slab->magic = SLAB_MAGIC;
    slab->cache = cache;
    slab->next = 0;
    slab->prev = 0;
    slab->inuse = 0;
    slab->total = (PAGE_SIZE - SLAB_OBJ_OFFSET) / cache->obj_size;
    slab->list_id = SLAB_LIST_NONE;

    if (slab->total == 0) {
        /* Object cannot fit beside the header (kmem_cache_create should
         * have refused such a cache; belt and braces). */
        slab->magic = 0;
        buddy_free(page, 0);
        return 0;
    }

    /* Chain every object through its first pointer-sized word. */
    uint8_t *obj_start = (uint8_t *)page + SLAB_OBJ_OFFSET;
    for (uint32_t i = 0; i + 1 < slab->total; i++) {
        *(void **)(obj_start + i * cache->obj_size) =
            (void *)(obj_start + (i + 1) * cache->obj_size);
    }
    *(void **)(obj_start + (slab->total - 1) * cache->obj_size) = 0;
    slab->free_list = obj_start;

    cache->num_slabs++;
    return slab;
}

/** Unlink and return a fully-free slab page to the buddy allocator. */
static void release_slab(kmem_cache_t *cache, slab_t *slab) {
    slab_list_remove(cache, slab);
    slab->magic = 0;  /* page no longer identifies as a slab */
    buddy_free(slab, 0);
    cache->num_slabs--;
}

/* ---- public cache API ----------------------------------------------------- */

void slab_init(void) {
    /* Unconditional reset: the kernel calls this once at boot; host unit
     * tests re-run it after re-initializing the buddy region. */
    for (int i = 0; i < MAX_SLABS; i++) {
        all_caches[i].name[0] = '\0';
        all_caches[i].obj_size = 0;
        all_caches[i].slabs_full = 0;
        all_caches[i].slabs_partial = 0;
        all_caches[i].slabs_free = 0;
        all_caches[i].num_slabs = 0;
        all_caches[i].num_active = 0;
    }
    num_caches = 0;

    for (int i = 0; i < KMALLOC_NUM_CLASSES; i++) {
        char name[SLAB_NAME_LEN] = "kmalloc-";
        append_u32(name, 8, kmalloc_class_size[i]);
        size_caches[i] = kmem_cache_create(name, kmalloc_class_size[i],
                                           0, 0, 0, 0);
    }
    slab_initialized = 1;
}

kmem_cache_t* kmem_cache_create(const char *name, uint32_t size, uint32_t align,
                                uint32_t flags, void (*ctor)(void *),
                                void (*dtor)(void *)) {
    if (!name || size == 0 || num_caches >= MAX_SLABS) {
        return 0;
    }

    uint32_t obj_size = size < (uint32_t)sizeof(void *)
                            ? (uint32_t)sizeof(void *) : size;
    obj_size = align_size(obj_size, align);
    if (obj_size > PAGE_SIZE - SLAB_OBJ_OFFSET) {
        return 0;  /* not even one object per slab: refuse (the AI-dump
                    * version underflowed total and scribbled memory) */
    }

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

    str_copy(cache->name, name, SLAB_NAME_LEN);
    cache->obj_size = obj_size;
    cache->align = align ? align : (uint32_t)sizeof(void *);
    cache->flags = flags;
    cache->slabs_full = 0;
    cache->slabs_partial = 0;
    cache->slabs_free = 0;
    cache->num_slabs = 0;
    cache->num_active = 0;
    cache->ctor = ctor;
    cache->dtor = dtor;

    num_caches++;
    return cache;
}

void kmem_cache_destroy(kmem_cache_t *cache) {
    if (!cache || cache->name[0] == '\0') {
        return;
    }
    while (cache->slabs_full) {
        release_slab(cache, cache->slabs_full);
    }
    while (cache->slabs_partial) {
        release_slab(cache, cache->slabs_partial);
    }
    while (cache->slabs_free) {
        release_slab(cache, cache->slabs_free);
    }
    cache->name[0] = '\0';
    cache->num_active = 0;
    num_caches--;
}

void* kmem_cache_alloc(kmem_cache_t *cache) {
    if (!cache) {
        return 0;
    }

    slab_t *slab = cache->slabs_partial;
    if (!slab) {
        slab = cache->slabs_free;
        if (slab) {
            slab_list_remove(cache, slab);
        } else {
            slab = alloc_slab(cache);
            if (!slab) {
                return 0;  /* buddy OOM propagates as NULL */
            }
        }
        slab_list_push(cache, slab, SLAB_LIST_PARTIAL);
    }

    void *obj = slab->free_list;
    slab->free_list = *(void **)obj;
    slab->inuse++;
    cache->num_active++;

    if (slab->inuse == slab->total) {
        slab_list_remove(cache, slab);
        slab_list_push(cache, slab, SLAB_LIST_FULL);
    }

    if (cache->ctor) {
        cache->ctor(obj);
    }
    return obj;
}

int kmem_cache_free(kmem_cache_t *cache, void *obj) {
    if (!cache || !obj) {
        return -1;
    }

    /* Recover and validate the slab header at the page base. */
    slab_t *slab = (slab_t *)((uintptr_t)obj & ~(uintptr_t)(PAGE_SIZE - 1));
    if (slab->magic != SLAB_MAGIC || slab->cache != cache) {
        return -1;  /* not a live slab page of this cache */
    }

    uint8_t *obj_start = (uint8_t *)slab + SLAB_OBJ_OFFSET;
    uintptr_t off = (uintptr_t)obj - (uintptr_t)obj_start;
    if ((uintptr_t)obj < (uintptr_t)obj_start ||
        off % cache->obj_size != 0 ||
        off / cache->obj_size >= slab->total) {
        return -1;  /* pointer does not address an object slot */
    }

    /* Double-free guard: reject objects already on the free list. */
    for (void *p = slab->free_list; p; p = *(void **)p) {
        if (p == obj) {
            return -1;
        }
    }

    if (cache->dtor) {
        cache->dtor(obj);
    }

    *(void **)obj = slab->free_list;
    slab->free_list = obj;
    slab->inuse--;
    cache->num_active--;

    if (slab->list_id == SLAB_LIST_FULL) {
        slab_list_remove(cache, slab);
        slab_list_push(cache, slab, SLAB_LIST_PARTIAL);
    }
    if (slab->inuse == 0) {
        slab_list_remove(cache, slab);
        if (cache->slabs_free) {
            /* Keep at most one empty slab cached per cache; return the
             * rest to the buddy so memory actually comes back. */
            slab->list_id = SLAB_LIST_NONE;
            slab->magic = 0;
            buddy_free(slab, 0);
            cache->num_slabs--;
        } else {
            slab_list_push(cache, slab, SLAB_LIST_FREE);
        }
    }
    return 0;
}

/* ---- kmalloc / kfree ------------------------------------------------------ */

void* kmalloc(uint32_t size) {
    if (!slab_initialized || size == 0) {
        return 0;
    }

    if (size <= KMALLOC_MAX_SLAB_SIZE) {
        for (int i = 0; i < KMALLOC_NUM_CLASSES; i++) {
            if (size <= kmalloc_class_size[i]) {
                return kmem_cache_alloc(size_caches[i]);
            }
        }
    }

    /* Large allocation: straight from the buddy (page-aligned result —
     * that alignment is exactly how kfree() tells the two paths apart). */
    uint32_t order = buddy_get_order(size);
    if (order >= MAX_ORDER) {
        return 0;
    }
    return buddy_alloc(order);
}

void* kzalloc(uint32_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        uint8_t *p = (uint8_t *)ptr;
        for (uint32_t i = 0; i < size; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr || !slab_initialized) {
        return;
    }
    if (!buddy_owns(ptr)) {
        return;  /* never ours (all kmalloc memory lives in buddy space) */
    }
    if (((uintptr_t)ptr & (PAGE_SIZE - 1)) == 0) {
        /* Page-aligned => buddy-direct large allocation. Slab objects can
         * never be page-aligned (they sit behind the in-page header).
         * buddy_free_block() looks up the recorded order and rejects
         * double frees. */
        buddy_free_block(ptr);
        return;
    }
    slab_t *slab = (slab_t *)((uintptr_t)ptr & ~(uintptr_t)(PAGE_SIZE - 1));
    if (slab->magic != SLAB_MAGIC || !slab->cache) {
        return;  /* not a slab object: ignore rather than corrupt */
    }
    kmem_cache_free(slab->cache, ptr);
}
