/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Unit tests for the REAL slab allocator / kmalloc / kfree
 * (kernel/mm/slab.c on top of kernel/mm/buddy.c), compiled directly into
 * the test runner — no mocks. The slab writes free lists INTO its pages,
 * so kmem_env_map_buddy_window() maps real host memory at the buddy base
 * (16MB) before each test.
 *
 * Contract under test (see include/kernel/memory.h):
 *   - kmalloc <= 1024 comes from size-class caches (8..1024); pointers
 *     are never page-aligned (objects sit behind the in-page header)
 *   - kmalloc > 1024 falls back to buddy_alloc(); always page-aligned
 *   - kfree routes by that alignment and survives double/invalid frees
 *   - empty slabs return to the buddy (at most one cached per cache)
 */

#include <string.h>

#include "../framework/unittest.h"
#include "../framework/kmem_env.h"
#include "../../include/kernel/memory.h"

#define WINDOW_BYTES (8u * 1024 * 1024)              /* 8MB test arena  */
#define WINDOW_PAGES (WINDOW_BYTES / PAGE_SIZE)      /* 2048 pages      */
#define OBJ_OFFSET   (((uint32_t)sizeof(slab_t) + 15u) & ~15u)

/* Page base of a slab object pointer. */
static slab_t* slab_of(void *obj) {
    return (slab_t *)((uintptr_t)obj & ~(uintptr_t)(PAGE_SIZE - 1));
}

static int slab_env_reset(void) {
    if (kmem_env_map_buddy_window(WINDOW_BYTES) != 0) {
        return -1;
    }
    if (buddy_init(BUDDY_WINDOW_BASE, BUDDY_WINDOW_BASE + WINDOW_BYTES) < 0) {
        return -1;
    }
    slab_init();
    return 0;
}

static int ctor_calls;
static int dtor_calls;
static void count_ctor(void *obj) { (void)obj; ctor_calls++; }
static void count_dtor(void *obj) { (void)obj; dtor_calls++; }

TEST_SUITE(slab_tests)

    // kmalloc routes each size to the smallest fitting size class
    TEST_CASE(kmalloc_size_class_routing)
        ASSERT_EQ(slab_env_reset(), 0, "test arena maps");
        struct { uint32_t req; uint32_t cls; } cases[] = {
            {1, 8}, {8, 8}, {9, 16}, {100, 128}, {512, 512}, {1024, 1024},
        };
        void *ptrs[6];
        for (int i = 0; i < 6; i++) {
            void *p = kmalloc(cases[i].req);
            ptrs[i] = p;
            ASSERT_NE((uintptr_t)p, (uintptr_t)0, "kmalloc succeeds");
            ASSERT_NE((uintptr_t)p % PAGE_SIZE, (uintptr_t)0,
                      "slab object is never page-aligned");
            ASSERT_EQ(slab_of(p)->magic, (uint32_t)SLAB_MAGIC,
                      "object's page carries the slab header");
            ASSERT_EQ(slab_of(p)->cache->obj_size, cases[i].cls,
                      "request routed to smallest fitting class");
            memset(p, 0xA5, cases[i].req); /* full claimed size is usable */
        }
        ASSERT_EQ((uintptr_t)kmalloc(0), (uintptr_t)0, "kmalloc(0) is NULL");
        for (int i = 0; i < 6; i++) {
            kfree(ptrs[i]);
        }
        ASSERT_EQ(slab_of(ptrs[0])->cache->num_active, 0u,
                  "no live objects after freeing");
    END_TEST_CASE()

    // Freed objects are reused (LIFO) instead of growing the slab
    TEST_CASE(reuse_after_free)
        ASSERT_EQ(slab_env_reset(), 0, "test arena maps");
        void *p1 = kmalloc(64);
        ASSERT_NE((uintptr_t)p1, (uintptr_t)0, "first alloc");
        kfree(p1);
        void *p2 = kmalloc(64);
        ASSERT_EQ((uintptr_t)p2, (uintptr_t)p1, "freed object is reused");

        /* Same via the raw cache API. */
        kmem_cache_t *c = kmem_cache_create("reuse-test", 40, 0, 0, 0, 0);
        ASSERT_NE((uintptr_t)c, (uintptr_t)0, "cache created");
        void *a = kmem_cache_alloc(c);
        void *b = kmem_cache_alloc(c);
        ASSERT_NE((uintptr_t)a, (uintptr_t)b, "distinct objects");
        ASSERT_EQ(kmem_cache_free(c, a), 0, "free accepted");
        void *a2 = kmem_cache_alloc(c);
        ASSERT_EQ((uintptr_t)a2, (uintptr_t)a, "cache reuses freed slot");
        kmem_cache_destroy(c);
        kfree(p2);
    END_TEST_CASE()

    // kzalloc returns zeroed memory even when recycling a dirty object
    TEST_CASE(kzalloc_zeroes_recycled_memory)
        ASSERT_EQ(slab_env_reset(), 0, "test arena maps");
        uint8_t *p = (uint8_t *)kmalloc(256);
        ASSERT_NE((uintptr_t)p, (uintptr_t)0, "dirty alloc");
        memset(p, 0xAA, 256);
        kfree(p);
        uint8_t *q = (uint8_t *)kzalloc(256);
        ASSERT_EQ((uintptr_t)q, (uintptr_t)p, "same object recycled");
        int dirty = 0;
        for (int i = 0; i < 256; i++) {
            if (q[i] != 0) dirty++;
        }
        ASSERT_EQ(dirty, 0, "kzalloc memory fully zeroed");
        kfree(q);
    END_TEST_CASE()

    // Allocations over 1024 bytes go straight to the buddy and back
    TEST_CASE(large_alloc_buddy_fallback)
        ASSERT_EQ(slab_env_reset(), 0, "test arena maps");
        uint32_t base_free = buddy_get_free_page_count();
        ASSERT_EQ(base_free, WINDOW_PAGES, "no slab pages allocated yet");

        void *p1 = kmalloc(1025);   /* just over the slab limit: 1 page  */
        void *p2 = kmalloc(4096);   /* exactly one page                  */
        void *p3 = kmalloc(12288);  /* 3 pages -> order 2 = 4 pages      */
        ASSERT_NE((uintptr_t)p1, (uintptr_t)0, "1025B alloc");
        ASSERT_NE((uintptr_t)p2, (uintptr_t)0, "4KB alloc");
        ASSERT_NE((uintptr_t)p3, (uintptr_t)0, "12KB alloc");
        ASSERT_EQ((uintptr_t)p1 % PAGE_SIZE, (uintptr_t)0,
                  "large allocs are page-aligned (buddy-direct)");
        ASSERT_EQ((uintptr_t)p2 % PAGE_SIZE, (uintptr_t)0, "page-aligned");
        ASSERT_EQ((uintptr_t)p3 % PAGE_SIZE, (uintptr_t)0, "page-aligned");
        ASSERT_EQ(buddy_get_free_page_count(), base_free - 1 - 1 - 4,
                  "buddy pages consumed: 1 + 1 + 4");
        memset(p3, 0x5A, 12288);

        kfree(p1);
        kfree(p2);
        kfree(p3);
        ASSERT_EQ(buddy_get_free_page_count(), base_free,
                  "kfree returned every buddy page");
        ASSERT_EQ(buddy_free_blocks_of_order(MAX_ORDER - 1),
                  WINDOW_PAGES >> (MAX_ORDER - 1),
                  "buddy fully re-coalesced after large kfrees");

        ASSERT_EQ((uintptr_t)kmalloc(5u * 1024 * 1024), (uintptr_t)0,
                  "requests beyond the largest buddy block fail cleanly");
    END_TEST_CASE()

    // Cache lifecycle: ctor/dtor, multi-slab growth, empty-slab release
    TEST_CASE(cache_lifecycle_and_slab_release)
        ASSERT_EQ(slab_env_reset(), 0, "test arena maps");
        uint32_t base_free = buddy_get_free_page_count();
        ctor_calls = 0;
        dtor_calls = 0;

        kmem_cache_t *c = kmem_cache_create("lifecycle", 48, 0, 0,
                                            count_ctor, count_dtor);
        ASSERT_NE((uintptr_t)c, (uintptr_t)0, "cache created");
        uint32_t per_slab = (PAGE_SIZE - OBJ_OFFSET) / c->obj_size;
        ASSERT_GT(per_slab, 1u, "multiple objects per slab");

        /* Force a second slab page. */
        static void *objs[512];
        uint32_t n = per_slab + 1;
        for (uint32_t i = 0; i < n; i++) {
            objs[i] = kmem_cache_alloc(c);
            ASSERT_NE((uintptr_t)objs[i], (uintptr_t)0, "cache alloc");
        }
        ASSERT_EQ(c->num_slabs, 2u, "second slab created on overflow");
        ASSERT_EQ(c->num_active, n, "active object accounting");
        ASSERT_EQ(ctor_calls, (int)n, "ctor ran per allocation");

        for (uint32_t i = 0; i < n; i++) {
            ASSERT_EQ(kmem_cache_free(c, objs[i]), 0, "cache free");
        }
        ASSERT_EQ(c->num_active, 0u, "no active objects left");
        ASSERT_EQ(dtor_calls, (int)n, "dtor ran per free");
        ASSERT_EQ(c->num_slabs, 1u,
                  "only one empty slab kept; the rest went back to buddy");

        kmem_cache_destroy(c);
        ASSERT_EQ(buddy_get_free_page_count(), base_free,
                  "destroy returned every slab page to the buddy");
    END_TEST_CASE()

    // Double frees and bogus pointers are rejected without corruption
    TEST_CASE(double_free_and_invalid_free_guards)
        ASSERT_EQ(slab_env_reset(), 0, "test arena maps");

        /* Slab-object double free via the raw API is reported... */
        kmem_cache_t *c = kmem_cache_create("guard", 32, 0, 0, 0, 0);
        void *o = kmem_cache_alloc(c);
        ASSERT_NE((uintptr_t)o, (uintptr_t)0, "alloc");
        ASSERT_EQ(kmem_cache_free(c, o), 0, "first free accepted");
        ASSERT_EQ(kmem_cache_free(c, o), -1, "double free rejected");
        ASSERT_EQ(kmem_cache_free(c, (uint8_t *)o + 1), -1,
                  "pointer between object slots rejected");
        kmem_cache_t *other = kmem_cache_create("guard2", 32, 0, 0, 0, 0);
        ASSERT_EQ(kmem_cache_free(other, o), -1, "wrong cache rejected");
        kmem_cache_destroy(other);
        kmem_cache_destroy(c);

        /* ...and kfree survives the same abuse silently: after a double
         * kfree, two fresh allocations must still be distinct objects
         * (a corrupted free list would hand the same address out twice). */
        void *p = kmalloc(32);
        ASSERT_NE((uintptr_t)p, (uintptr_t)0, "kmalloc");
        kfree(p);
        kfree(p); /* double free: must be ignored */
        void *q1 = kmalloc(32);
        void *q2 = kmalloc(32);
        ASSERT_NE((uintptr_t)q1, (uintptr_t)0, "alloc after double free");
        ASSERT_NE((uintptr_t)q2, (uintptr_t)0, "alloc after double free");
        ASSERT_NE((uintptr_t)q1, (uintptr_t)q2,
                  "free list not corrupted by double kfree");

        /* Large-allocation (buddy) double free. */
        void *b = kmalloc(8192);
        uint32_t free_now = buddy_get_free_page_count();
        kfree(b);
        ASSERT_EQ(buddy_get_free_page_count(), free_now + 2, "pages returned");
        kfree(b); /* double free: must be ignored */
        ASSERT_EQ(buddy_get_free_page_count(), free_now + 2,
                  "double kfree of a buddy block changes nothing");

        /* Pointers that were never kmalloc'd. */
        int on_stack;
        kfree((void *)0);      /* NULL     */
        kfree(&on_stack);      /* stack    */
        kfree((void *)0x1000); /* below the buddy region */
        ASSERT_EQ(buddy_get_free_page_count(), free_now + 2,
                  "foreign pointers ignored by kfree");
        kfree(q1);
        kfree(q2);
    END_TEST_CASE()

    // Buddy exhaustion propagates as NULL and recovers after kfree
    TEST_CASE(oom_and_recovery)
        /* Tiny arena: 16 pages. Each kmalloc-1024 slab page holds
         * (4096 - header) / 1024 = 3 objects. */
        ASSERT_EQ(kmem_env_map_buddy_window(16 * PAGE_SIZE), 0, "arena maps");
        ASSERT_EQ(buddy_init(BUDDY_WINDOW_BASE,
                             BUDDY_WINDOW_BASE + 16 * PAGE_SIZE), 16,
                  "16-page buddy region");
        slab_init();

        uint32_t per_slab = (PAGE_SIZE - OBJ_OFFSET) / 1024u;
        uint32_t capacity = 16u * per_slab;
        static void *ptrs[16 * 3];
        uint32_t got = 0;
        for (;;) {
            void *p = kmalloc(1024);
            if (!p) break;
            ptrs[got++] = p;
            ASSERT_LT(got, capacity + 1u, "cannot exceed arena capacity");
        }
        ASSERT_EQ(got, capacity, "every slab slot allocatable before OOM");
        ASSERT_EQ((uintptr_t)kmalloc(1024), (uintptr_t)0, "slab path OOM: NULL");
        ASSERT_EQ((uintptr_t)kmalloc(8), (uintptr_t)0,
                  "other size classes hit the same buddy OOM");
        ASSERT_EQ((uintptr_t)kmalloc(4096), (uintptr_t)0, "buddy path OOM: NULL");

        for (uint32_t i = 0; i < got; i++) {
            kfree(ptrs[i]);
        }
        /* All slabs emptied: at most one page stays cached per cache. */
        ASSERT_GT(buddy_get_free_page_count(), 14u, "pages returned to buddy");

        void *again = kmalloc(1024);
        ASSERT_NE((uintptr_t)again, (uintptr_t)0, "allocator recovers");
        kfree(again);
    END_TEST_CASE()

    // Cache creation limits: oversize objects and NULL handling
    TEST_CASE(cache_creation_limits)
        ASSERT_EQ(slab_env_reset(), 0, "test arena maps");
        ASSERT_EQ((uintptr_t)kmem_cache_create("too-big", PAGE_SIZE, 0, 0, 0, 0),
                  (uintptr_t)0,
                  "object that cannot fit beside the header is refused");
        ASSERT_EQ((uintptr_t)kmem_cache_create((const char *)0, 8, 0, 0, 0, 0),
                  (uintptr_t)0, "NULL name refused");
        ASSERT_EQ((uintptr_t)kmem_cache_create("zero", 0, 0, 0, 0, 0),
                  (uintptr_t)0, "zero size refused");
        ASSERT_EQ((uintptr_t)kmem_cache_alloc((kmem_cache_t *)0), (uintptr_t)0,
                  "alloc from NULL cache is NULL");
        ASSERT_EQ(kmem_cache_free((kmem_cache_t *)0, (void *)0), -1,
                  "free to NULL cache rejected");

        /* A cache of nearly-page-size objects still works (1/slab). */
        kmem_cache_t *big = kmem_cache_create("big", 2048, 0, 0, 0, 0);
        ASSERT_NE((uintptr_t)big, (uintptr_t)0, "2KB object cache created");
        void *x = kmem_cache_alloc(big);
        ASSERT_NE((uintptr_t)x, (uintptr_t)0, "2KB object allocated");
        memset(x, 0x77, 2048);
        ASSERT_EQ(kmem_cache_free(big, x), 0, "2KB object freed");
        kmem_cache_destroy(big);
    END_TEST_CASE()

END_TEST_SUITE()
