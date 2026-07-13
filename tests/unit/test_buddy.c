/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Unit tests for the REAL buddy allocator (kernel/mm/buddy.c), compiled
 * directly into the test runner — no mocks. The buddy keeps all of its
 * bookkeeping in a static descriptor table and never dereferences the
 * memory it manages, so these tests can drive it over the kernel's real
 * physical range [16MB, ...) without mapping anything host-side.
 *
 * Contract under test (see include/kernel/memory.h):
 *   - orders 0..MAX_ORDER-1 (10), max block 4MB
 *   - buddy_free validates head/order: double frees, wrong orders and
 *     mid-block pointers are rejected with -1
 *   - full coalescing: any alloc/free sequence that returns every page
 *     restores the initial max-order block population
 *   - buddy_init_from_bootinfo excludes non-USABLE regions and the
 *     framebuffer, and falls back to [16MB, 112MB) without bootinfo
 */

#include <string.h>

#include "../framework/unittest.h"
#include "../../include/kernel/memory.h"

#define KB(x) ((uint32_t)(x) * 1024u)
#define MB(x) ((uint32_t)(x) * 1024u * 1024u)

/* Standard test region: [16MB, 32MB) = 4096 pages = 4 order-10 blocks. */
#define REG_START MB(16)
#define REG_END   MB(32)
#define REG_PAGES 4096u
#define TOP_ORDER (MAX_ORDER - 1)          /* 10 */
#define TOP_BLOCKS (REG_PAGES >> TOP_ORDER) /* 4 */

TEST_SUITE(buddy_tests)

    // Region setup, initial free-list population, introspection helpers
    TEST_CASE(init_state)
        int managed = buddy_init(REG_START, REG_END);
        ASSERT_EQ(managed, (int)REG_PAGES, "16MB span = 4096 managed pages");
        ASSERT_EQ(buddy_get_managed_pages(), REG_PAGES, "managed count getter");
        ASSERT_EQ(buddy_get_free_page_count(), REG_PAGES, "everything starts free");
        ASSERT_EQ(buddy_free_blocks_of_order(TOP_ORDER), TOP_BLOCKS,
                  "init coalesces to max-order blocks");
        for (uint32_t o = 0; o < TOP_ORDER; o++) {
            ASSERT_EQ(buddy_free_blocks_of_order(o), 0u,
                      "no fragments below max order after init");
        }
        uint32_t s = 0, e = 0;
        buddy_get_region(&s, &e);
        ASSERT_EQ(s, REG_START, "region start reported");
        ASSERT_EQ(e, REG_END, "region end reported");
        ASSERT_TRUE(buddy_owns((void *)(uintptr_t)REG_START), "owns first page");
        ASSERT_FALSE(buddy_owns((void *)(uintptr_t)(REG_START - 1)), "not below");
        ASSERT_FALSE(buddy_owns((void *)(uintptr_t)REG_END), "not at end");
        ASSERT_EQ(buddy_init(MB(20), MB(20)), -1, "empty range rejected");
        ASSERT_EQ(buddy_init(MB(32), MB(16)), -1, "inverted range rejected");
        buddy_init(REG_START, REG_END); /* leave a valid region behind */
    END_TEST_CASE()

    // buddy_get_order boundaries, incl. the oversize sentinel
    TEST_CASE(get_order_boundaries)
        ASSERT_EQ(buddy_get_order(1), 0u, "1 byte -> order 0");
        ASSERT_EQ(buddy_get_order(KB(4)), 0u, "4KB -> order 0");
        ASSERT_EQ(buddy_get_order(KB(4) + 1), 1u, "4KB+1 -> order 1");
        ASSERT_EQ(buddy_get_order(KB(8)), 1u, "8KB -> order 1");
        ASSERT_EQ(buddy_get_order(MB(4)), (uint32_t)TOP_ORDER, "4MB -> order 10");
        ASSERT_EQ(buddy_get_order(MB(4) + 1), (uint32_t)MAX_ORDER,
                  "over 4MB -> invalid order sentinel, never a truncated block");
    END_TEST_CASE()

    // Round-trips at several orders: success, alignment, exact accounting
    TEST_CASE(roundtrip_multi_order)
        buddy_init(REG_START, REG_END);
        uint32_t orders[] = {0, 1, 2, 5, TOP_ORDER};
        for (unsigned i = 0; i < sizeof(orders) / sizeof(orders[0]); i++) {
            uint32_t o = orders[i];
            void *p = buddy_alloc(o);
            ASSERT_NE((uintptr_t)p, (uintptr_t)0, "allocation succeeds");
            ASSERT_TRUE(buddy_owns(p), "block inside region");
            ASSERT_EQ((uintptr_t)p % ((uintptr_t)PAGE_SIZE << o), (uintptr_t)0,
                      "block naturally aligned to its size");
            ASSERT_EQ(buddy_get_free_page_count(), REG_PAGES - (1u << o),
                      "free count drops by block size");
            ASSERT_EQ(buddy_free(p, o), 0, "free accepted");
            ASSERT_EQ(buddy_get_free_page_count(), REG_PAGES,
                      "free count restored");
            ASSERT_EQ(buddy_free_blocks_of_order(TOP_ORDER), TOP_BLOCKS,
                      "coalesced back to max-order blocks");
        }
    END_TEST_CASE()

    // A single order-0 alloc splits one max block into a full chain
    TEST_CASE(split_chain_state)
        buddy_init(REG_START, REG_END);
        void *p = buddy_alloc(0);
        ASSERT_NE((uintptr_t)p, (uintptr_t)0, "order-0 alloc succeeds");
        for (uint32_t o = 0; o < TOP_ORDER; o++) {
            ASSERT_EQ(buddy_free_blocks_of_order(o), 1u,
                      "each order keeps exactly one split remainder");
        }
        ASSERT_EQ(buddy_free_blocks_of_order(TOP_ORDER), TOP_BLOCKS - 1,
                  "one max block was consumed by the split");
        ASSERT_EQ(buddy_get_free_page_count(), REG_PAGES - 1, "one page in use");
        ASSERT_EQ(buddy_free(p, 0), 0, "free accepted");
        ASSERT_EQ(buddy_free_blocks_of_order(TOP_ORDER), TOP_BLOCKS,
                  "chain fully re-coalesced");
    END_TEST_CASE()

    // Full fragmentation into order-0 pages, then full coalescing back
    TEST_CASE(fragmentation_then_full_coalesce)
        static void *ptrs[REG_PAGES];
        buddy_init(REG_START, REG_END);

        for (uint32_t i = 0; i < REG_PAGES; i++) {
            ptrs[i] = buddy_alloc(0);
            ASSERT_NE((uintptr_t)ptrs[i], (uintptr_t)0, "page alloc succeeds");
        }
        ASSERT_EQ(buddy_get_free_page_count(), 0u, "all pages handed out");
        ASSERT_EQ((uintptr_t)buddy_alloc(0), (uintptr_t)0, "OOM returns NULL");

        /* Free in a non-sequential order (evens then odds) so coalescing
         * has to match buddies across the whole region. */
        for (uint32_t i = 0; i < REG_PAGES; i += 2) {
            ASSERT_EQ(buddy_free(ptrs[i], 0), 0, "even page freed");
        }
        for (uint32_t i = 1; i < REG_PAGES; i += 2) {
            ASSERT_EQ(buddy_free(ptrs[i], 0), 0, "odd page freed");
        }
        ASSERT_EQ(buddy_get_free_page_count(), REG_PAGES, "all pages free");
        ASSERT_EQ(buddy_free_blocks_of_order(TOP_ORDER), TOP_BLOCKS,
                  "region coalesced back to max-order blocks");
        for (uint32_t o = 0; o < TOP_ORDER; o++) {
            ASSERT_EQ(buddy_free_blocks_of_order(o), 0u, "no leftover fragments");
        }
    END_TEST_CASE()

    // OOM at max order, oversize rejections, zero-page requests
    TEST_CASE(oom_behavior)
        buddy_init(REG_START, REG_END);
        void *blocks[TOP_BLOCKS];
        for (uint32_t i = 0; i < TOP_BLOCKS; i++) {
            blocks[i] = buddy_alloc(TOP_ORDER);
            ASSERT_NE((uintptr_t)blocks[i], (uintptr_t)0, "max block alloc");
        }
        ASSERT_EQ((uintptr_t)buddy_alloc(TOP_ORDER), (uintptr_t)0,
                  "5th max block: OOM returns NULL");
        ASSERT_EQ((uintptr_t)buddy_alloc(0), (uintptr_t)0,
                  "no pages left for any order");
        ASSERT_EQ((uintptr_t)buddy_alloc(MAX_ORDER), (uintptr_t)0,
                  "invalid order rejected");
        ASSERT_EQ((uintptr_t)buddy_alloc_pages(0), (uintptr_t)0,
                  "zero pages rejected");
        ASSERT_EQ((uintptr_t)buddy_alloc_pages(1025), (uintptr_t)0,
                  "requests beyond the largest block rejected");
        for (uint32_t i = 0; i < TOP_BLOCKS; i++) {
            ASSERT_EQ(buddy_free(blocks[i], TOP_ORDER), 0, "max block freed");
        }
        ASSERT_EQ(buddy_get_free_page_count(), REG_PAGES, "all pages back");
    END_TEST_CASE()

    // Free validation: double free, wrong order, mid-block, foreign addr
    TEST_CASE(free_validation_guards)
        buddy_init(REG_START, REG_END);
        void *p = buddy_alloc(3); /* 8 pages */
        ASSERT_NE((uintptr_t)p, (uintptr_t)0, "order-3 alloc succeeds");

        ASSERT_EQ(buddy_free(p, 2), -1, "wrong order rejected");
        ASSERT_EQ(buddy_free((uint8_t *)p + PAGE_SIZE, 3), -1,
                  "mid-block pointer rejected");
        ASSERT_EQ(buddy_free((uint8_t *)p + 1, 3), -1,
                  "unaligned pointer rejected");
        ASSERT_EQ(buddy_free((void *)(uintptr_t)(REG_START - PAGE_SIZE), 0), -1,
                  "address below region rejected");
        ASSERT_EQ(buddy_free((void *)0, 0), -1, "NULL rejected");
        ASSERT_EQ(buddy_get_free_page_count(), REG_PAGES - 8u,
                  "rejected frees change nothing");

        ASSERT_EQ(buddy_free(p, 3), 0, "correct free accepted");
        ASSERT_EQ(buddy_free(p, 3), -1, "DOUBLE free rejected");
        ASSERT_EQ(buddy_free_block(p), -1, "double free via free_block rejected");
        ASSERT_EQ(buddy_get_free_page_count(), REG_PAGES,
                  "double free did not corrupt accounting");

        void *q = buddy_alloc(3);
        ASSERT_EQ((uintptr_t)q, (uintptr_t)p, "block reusable after free");
        ASSERT_EQ(buddy_free_block(q), 3, "free_block returns recorded order");
    END_TEST_CASE()

    // Page-count wrappers round up to the next power of two
    TEST_CASE(alloc_pages_roundtrip)
        buddy_init(REG_START, REG_END);
        void *p = buddy_alloc_pages(3); /* rounds to order 2 = 4 pages */
        ASSERT_NE((uintptr_t)p, (uintptr_t)0, "3-page request succeeds");
        ASSERT_EQ(buddy_get_free_page_count(), REG_PAGES - 4u,
                  "3 pages rounded up to a 4-page block");
        ASSERT_EQ(buddy_free_pages(p, 3), 0, "free via same page count");
        ASSERT_EQ(buddy_get_free_page_count(), REG_PAGES, "all pages back");
        ASSERT_EQ(buddy_free_pages(p, 3), -1, "double free_pages rejected");
    END_TEST_CASE()

    // Bootinfo derivation: USABLE only, minus non-USABLE holes and the fb
    TEST_CASE(bootinfo_map_with_holes)
        /* Sorted, non-overlapping map (spec §4): usable RAM up to 128MB
         * with a 1MB reserved hole at 48MB, plus a 64KB framebuffer at
         * 80MB that the map itself calls USABLE (fb must still win). */
        static const tocinboot_mmap_entry map[] = {
            { 0x00000000u, 0x03000000u, TOCINBOOT_MEM_USABLE,   0 },
            { 0x03000000u, 0x00100000u, TOCINBOOT_MEM_RESERVED, 0 },
            { 0x03100000u, 0x0AF00000u, TOCINBOOT_MEM_USABLE,   0 }, /* to 224MB */
        };
        tocinboot_info info;
        memset(&info, 0, sizeof(info));
        info.magic = TOCINBOOT_INFO_MAGIC;
        info.version = TOCINBOOT_VERSION;
        info.flags = TOCINBOOT_F_UEFI | TOCINBOOT_F_FB;
        info.memmap_addr = (tb_u64)(uintptr_t)map;
        info.memmap_count = sizeof(map) / sizeof(map[0]);
        info.memmap_entry_size = sizeof(tocinboot_mmap_entry);
        info.fb_base = 0x05000000u; /* 80MB, inside a USABLE entry */
        info.fb_pitch = 4096;
        info.fb_height = 16;        /* 64KB = 16 pages */

        int managed = buddy_init_from_bootinfo(&info);
        /* [16MB, 128MB) = 28672 pages, minus 256 (reserved MB) minus 16 (fb) */
        ASSERT_EQ(managed, 28672 - 256 - 16, "holes excluded from management");
        ASSERT_EQ(buddy_get_managed_pages(), 28672u - 272u, "managed getter");
        ASSERT_EQ(buddy_get_free_page_count(), 28672u - 272u, "all managed free");

        uint32_t s = 0, e = 0;
        buddy_get_region(&s, &e);
        ASSERT_EQ(s, (uint32_t)BUDDY_REGION_START, "region starts at 16MB");
        ASSERT_EQ(e, (uint32_t)BUDDY_REGION_LIMIT, "region capped at 128MB");

        ASSERT_TRUE(buddy_addr_is_managed(0x01000000u), "16MB page managed");
        ASSERT_FALSE(buddy_addr_is_managed(0x03000000u), "reserved hole excluded");
        ASSERT_FALSE(buddy_addr_is_managed(0x030FF000u), "last hole page excluded");
        ASSERT_TRUE(buddy_addr_is_managed(0x03100000u), "page after hole managed");
        ASSERT_FALSE(buddy_addr_is_managed(0x05000000u), "framebuffer excluded");
        ASSERT_FALSE(buddy_addr_is_managed(0x0500F000u), "last fb page excluded");
        ASSERT_TRUE(buddy_addr_is_managed(0x05010000u), "page after fb managed");
        ASSERT_FALSE(buddy_addr_is_managed(0x00F00000u), "below 16MB not buddy's");

        /* Drain everything: no returned block may touch a hole. */
        uint32_t got = 0, bad = 0;
        for (;;) {
            void *p = buddy_alloc(0);
            if (!p) break;
            uintptr_t a = (uintptr_t)p;
            if ((a >= 0x03000000u && a < 0x03100000u) ||
                (a >= 0x05000000u && a < 0x05010000u)) {
                bad++;
            }
            got++;
        }
        ASSERT_EQ(bad, 0u, "no allocation lands in a hole");
        ASSERT_EQ(got, 28672u - 272u, "exactly the managed pages allocatable");
    END_TEST_CASE()

    // NULL bootinfo degrades to the fixed [16MB, 112MB) fallback
    TEST_CASE(bootinfo_null_fallback)
        int managed = buddy_init_from_bootinfo((const tocinboot_info *)0);
        ASSERT_EQ(managed, 24576, "fallback manages [16MB, 112MB) = 96MB");
        uint32_t s = 0, e = 0;
        buddy_get_region(&s, &e);
        ASSERT_EQ(s, (uint32_t)BUDDY_REGION_START, "fallback start 16MB");
        ASSERT_EQ(e, (uint32_t)BUDDY_FALLBACK_END, "fallback end 112MB");
        ASSERT_EQ(buddy_free_blocks_of_order(TOP_ORDER), 24u,
                  "96MB coalesces into 24 max-order blocks");
    END_TEST_CASE()

END_TEST_SUITE()
