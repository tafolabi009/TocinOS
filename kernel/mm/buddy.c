/**
 * TocinOS Buddy Allocator
 *
 * Binary buddy allocator for the HIGH physical region [16MB, top). The
 * low region [0, 16MB) stays with the PMM bitmap allocator; kernel.c
 * reserves every buddy-managed page in the PMM bitmap at boot so the two
 * allocators can never hand out the same frame (see memory.h for the
 * documented split).
 *
 * Design notes:
 *  - All bookkeeping lives in a static descriptor table (6 bytes/page,
 *    up to 28672 pages = 112MB). The allocator NEVER dereferences the
 *    memory it manages, so it can be initialized before the region is
 *    virtually mapped, and host unit tests can drive it over address
 *    ranges that are not mapped in the test process at all.
 *  - Buddy pairing is done on page INDICES relative to the region start
 *    (idx ^ (1 << order)), so the region start only needs page alignment
 *    for internal consistency. Natural physical alignment of returned
 *    blocks additionally requires the region start to be aligned to the
 *    largest block size; the kernel's 16MB start satisfies that.
 *  - Every page carries an explicit state (reserved / free head / free
 *    tail / allocated head / allocated tail) and the head of each block
 *    records its order. buddy_free() validates against that record, which
 *    yields a real double-free / wrong-order / mid-block-pointer guard
 *    and O(1) coalescing checks (the AI-dump predecessor walked the free
 *    list on every merge step and corrupted it on double frees).
 *
 * This file replaces an earlier non-functional AI-generated draft; see
 * tests/unit/test_buddy.c for the behavioral contract.
 */

#include "../include/kernel/memory.h"

#define BUDDY_MAX_PAGES ((BUDDY_REGION_LIMIT - BUDDY_REGION_START) / PAGE_SIZE)

#define BP_NIL 0xFFFFu  /* free-list terminator (fits: capacity < 65535) */

/* Page states. RESERVED pages are inside the region span but never
 * allocatable (bootinfo holes, framebuffer, beyond-usable tail). */
enum {
    BP_RESERVED = 0,
    BP_FREE_HEAD,   /* head page of a free block (on a free list)   */
    BP_FREE_TAIL,   /* interior page of a free block                */
    BP_ALLOC_HEAD,  /* head page of a live allocation               */
    BP_ALLOC_TAIL,  /* interior page of a live allocation           */
};

typedef struct {
    uint16_t next;   /* free-list links (page indices), BP_NIL = none */
    uint16_t prev;
    uint8_t order;   /* meaningful on FREE_HEAD / ALLOC_HEAD pages    */
    uint8_t state;   /* BP_* */
} bpage_t;

static bpage_t page_desc[BUDDY_MAX_PAGES];
static uint16_t free_head[MAX_ORDER];
static uint32_t free_blocks[MAX_ORDER];

static uint32_t region_start;   /* page-aligned span, [region_start, region_end) */
static uint32_t region_end;
static uint32_t span_pages;     /* pages in the span, incl. reserved holes */
static uint32_t managed_pages;  /* allocatable pages (holes excluded)      */
static uint32_t free_pages_now; /* currently free pages                    */
static int buddy_ready;

/* ---- free list primitives ---------------------------------------------- */

static void list_push(uint32_t idx, uint32_t order) {
    page_desc[idx].state = BP_FREE_HEAD;
    page_desc[idx].order = (uint8_t)order;
    page_desc[idx].prev = BP_NIL;
    page_desc[idx].next = free_head[order];
    if (free_head[order] != BP_NIL) {
        page_desc[free_head[order]].prev = (uint16_t)idx;
    }
    free_head[order] = (uint16_t)idx;
    free_blocks[order]++;
}

static void list_unlink(uint32_t idx, uint32_t order) {
    if (page_desc[idx].prev != BP_NIL) {
        page_desc[page_desc[idx].prev].next = page_desc[idx].next;
    } else {
        free_head[order] = page_desc[idx].next;
    }
    if (page_desc[idx].next != BP_NIL) {
        page_desc[page_desc[idx].next].prev = page_desc[idx].prev;
    }
    page_desc[idx].next = BP_NIL;
    page_desc[idx].prev = BP_NIL;
    free_blocks[order]--;
}

/**
 * Insert the block headed at idx as free at the given order, merging with
 * its buddy as long as the buddy is a free block of the same order. The
 * caller must have already accounted the pages as free and must not have
 * idx on any list.
 */
static void insert_and_coalesce(uint32_t idx, uint32_t order) {
    while (order + 1 < MAX_ORDER) {
        uint32_t bud = idx ^ (1u << order);
        if (bud >= span_pages) {
            break;  /* buddy would fall outside the span */
        }
        if (page_desc[bud].state != BP_FREE_HEAD ||
            page_desc[bud].order != order) {
            break;  /* buddy not free at this order: cannot merge */
        }
        list_unlink(bud, order);
        /* The higher-address head becomes an interior page. */
        uint32_t head = (idx < bud) ? idx : bud;
        uint32_t other = (idx < bud) ? bud : idx;
        page_desc[other].state = BP_FREE_TAIL;
        idx = head;
        order++;
    }
    list_push(idx, order);
}

/* ---- initialization ------------------------------------------------------ */

/** Reset all bookkeeping over [start, end); every page starts RESERVED. */
static int buddy_setup(uint32_t start_addr, uint32_t end_addr) {
    start_addr = (start_addr + PAGE_SIZE - 1) & ~(uint32_t)(PAGE_SIZE - 1);
    end_addr &= ~(uint32_t)(PAGE_SIZE - 1);
    if (end_addr <= start_addr) {
        return -1;
    }

    span_pages = (end_addr - start_addr) / PAGE_SIZE;
    if (span_pages > BUDDY_MAX_PAGES) {
        span_pages = BUDDY_MAX_PAGES;
        end_addr = start_addr + span_pages * PAGE_SIZE;
    }
    region_start = start_addr;
    region_end = end_addr;
    managed_pages = 0;
    free_pages_now = 0;
    buddy_ready = 0;

    for (uint32_t i = 0; i < MAX_ORDER; i++) {
        free_head[i] = BP_NIL;
        free_blocks[i] = 0;
    }
    for (uint32_t i = 0; i < span_pages; i++) {
        page_desc[i].next = BP_NIL;
        page_desc[i].prev = BP_NIL;
        page_desc[i].order = 0;
        page_desc[i].state = BP_RESERVED;
    }
    return 0;
}

/** Hand one RESERVED page to the allocator (with coalescing). */
static void release_page(uint32_t idx) {
    managed_pages++;
    free_pages_now++;
    insert_and_coalesce(idx, 0);
}

int buddy_init(uint32_t start_addr, uint32_t end_addr) {
    if (buddy_setup(start_addr, end_addr) != 0) {
        return -1;
    }
    for (uint32_t i = 0; i < span_pages; i++) {
        release_page(i);
    }
    buddy_ready = 1;
    return (int)managed_pages;
}

int buddy_init_from_bootinfo(const tocinboot_info *info) {
    /* Page-usability scratch bitmap; only touched during init. */
    static uint8_t usable_map[BUDDY_MAX_PAGES / 8];

    if (!info || info->memmap_addr == 0 || info->memmap_count == 0 ||
        info->memmap_entry_size < sizeof(tocinboot_mmap_entry)) {
        /* No trustworthy memory map: fixed conservative fallback inside
         * the 128MB assumption (same policy as the PMM). */
        return buddy_init(BUDDY_REGION_START, BUDDY_FALLBACK_END);
    }

    if (buddy_setup(BUDDY_REGION_START, BUDDY_REGION_LIMIT) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < sizeof(usable_map); i++) {
        usable_map[i] = 0;
    }

    /* Pass 1: mark pages FULLY covered by USABLE entries.
     * Pass 2: clear pages with ANY overlap of a non-USABLE entry.
     * This stays correct even if the map were unsorted or overlapping.
     * Iterate by memmap_entry_size, never sizeof (spec §8.6). */
    const unsigned char *base =
        (const unsigned char *)(uintptr_t)info->memmap_addr;
    for (int pass = 0; pass < 2; pass++) {
        const unsigned char *p = base;
        for (tb_u32 i = 0; i < info->memmap_count; i++) {
            const tocinboot_mmap_entry *e = (const tocinboot_mmap_entry *)p;
            p += info->memmap_entry_size;

            tb_u64 lo = e->base;
            tb_u64 hi = e->base + e->length;
            if (hi < lo) {
                hi = (tb_u64)0xFFFFFFFFFFFFFFFFull;  /* u64 wrap: clamp */
            }
            if (e->type == TOCINBOOT_MEM_USABLE) {
                if (pass != 0) continue;
                /* fully covered pages only */
                lo = (lo + PAGE_SIZE - 1) & ~(tb_u64)(PAGE_SIZE - 1);
                hi &= ~(tb_u64)(PAGE_SIZE - 1);
            } else {
                if (pass != 1) continue;
                /* any overlap */
                lo &= ~(tb_u64)(PAGE_SIZE - 1);
                hi = (hi + PAGE_SIZE - 1) & ~(tb_u64)(PAGE_SIZE - 1);
            }
            if (hi <= (tb_u64)region_start || lo >= (tb_u64)region_end) {
                continue;
            }
            if (lo < (tb_u64)region_start) lo = region_start;
            if (hi > (tb_u64)region_end) hi = region_end;

            uint32_t first = (uint32_t)((lo - region_start) / PAGE_SIZE);
            uint32_t last = (uint32_t)((hi - region_start) / PAGE_SIZE);
            for (uint32_t pg = first; pg < last; pg++) {
                if (pass == 0) {
                    usable_map[pg / 8] |= (uint8_t)(1u << (pg % 8));
                } else {
                    usable_map[pg / 8] &= (uint8_t)~(1u << (pg % 8));
                }
            }
        }
    }

    /* Framebuffer is device memory even when a sloppy map calls it usable. */
    if ((info->flags & TOCINBOOT_F_FB) && info->fb_base != 0) {
        tb_u64 lo = info->fb_base & ~(tb_u64)(PAGE_SIZE - 1);
        tb_u64 hi = info->fb_base +
                    (tb_u64)info->fb_pitch * (tb_u64)info->fb_height;
        hi = (hi + PAGE_SIZE - 1) & ~(tb_u64)(PAGE_SIZE - 1);
        if (hi > (tb_u64)region_start && lo < (tb_u64)region_end) {
            if (lo < (tb_u64)region_start) lo = region_start;
            if (hi > (tb_u64)region_end) hi = region_end;
            uint32_t first = (uint32_t)((lo - region_start) / PAGE_SIZE);
            uint32_t last = (uint32_t)((hi - region_start) / PAGE_SIZE);
            for (uint32_t pg = first; pg < last; pg++) {
                usable_map[pg / 8] &= (uint8_t)~(1u << (pg % 8));
            }
        }
    }

    for (uint32_t pg = 0; pg < span_pages; pg++) {
        if (usable_map[pg / 8] & (1u << (pg % 8))) {
            release_page(pg);
        }
    }
    buddy_ready = 1;
    return (int)managed_pages;
}

/* ---- allocation ---------------------------------------------------------- */

void* buddy_alloc(uint32_t order) {
    if (!buddy_ready || order >= MAX_ORDER) {
        return 0;
    }

    /* Smallest available order >= requested. */
    uint32_t k = order;
    while (k < MAX_ORDER && free_head[k] == BP_NIL) {
        k++;
    }
    if (k >= MAX_ORDER) {
        return 0;  /* out of memory (for this order) */
    }

    uint32_t idx = free_head[k];
    list_unlink(idx, k);

    /* Split down, pushing each upper half back as a free block. Splits
     * must NOT coalesce (their buddy is the block being handed out). */
    while (k > order) {
        k--;
        list_push(idx + (1u << k), k);
    }

    page_desc[idx].state = BP_ALLOC_HEAD;
    page_desc[idx].order = (uint8_t)order;
    for (uint32_t i = 1; i < (1u << order); i++) {
        page_desc[idx + i].state = BP_ALLOC_TAIL;
    }
    free_pages_now -= (1u << order);

    return (void *)(uintptr_t)(region_start + idx * PAGE_SIZE);
}

/* Validated free core: addr must be the head of a live allocation. */
static int free_checked(void *addr, int expected_order) {
    if (!buddy_ready || !addr) {
        return -1;
    }
    uintptr_t a = (uintptr_t)addr;
    if (a < region_start || a >= region_end || (a & (PAGE_SIZE - 1))) {
        return -1;  /* foreign or unaligned address */
    }
    uint32_t idx = (uint32_t)((a - region_start) / PAGE_SIZE);
    if (page_desc[idx].state != BP_ALLOC_HEAD) {
        return -1;  /* double free, mid-block pointer, or never allocated */
    }
    uint32_t order = page_desc[idx].order;
    if (expected_order >= 0 && (uint32_t)expected_order != order) {
        return -1;  /* caller lied about the order */
    }

    for (uint32_t i = 1; i < (1u << order); i++) {
        page_desc[idx + i].state = BP_FREE_TAIL;
    }
    free_pages_now += (1u << order);
    insert_and_coalesce(idx, order);
    return (int)order;
}

int buddy_free(void *addr, uint32_t order) {
    if (order >= MAX_ORDER) {
        return -1;
    }
    return free_checked(addr, (int)order) < 0 ? -1 : 0;
}

int buddy_free_block(void *addr) {
    return free_checked(addr, -1);
}

/* ---- page-count helpers -------------------------------------------------- */

static uint32_t order_for_pages(uint32_t num_pages) {
    uint32_t order = 0;
    while (order < MAX_ORDER && (1u << order) < num_pages) {
        order++;
    }
    return order;  /* MAX_ORDER when num_pages > largest block */
}

void* buddy_alloc_pages(uint32_t num_pages) {
    if (num_pages == 0) {
        return 0;
    }
    uint32_t order = order_for_pages(num_pages);
    if (order >= MAX_ORDER) {
        return 0;  /* request larger than the largest block: refuse,
                    * never hand back a silently-truncated block */
    }
    return buddy_alloc(order);
}

int buddy_free_pages(void *addr, uint32_t num_pages) {
    if (num_pages == 0) {
        return -1;
    }
    uint32_t order = order_for_pages(num_pages);
    if (order >= MAX_ORDER) {
        return -1;
    }
    return buddy_free(addr, order);
}

uint32_t buddy_get_order(uint32_t size) {
    if (size <= PAGE_SIZE) {
        return 0;
    }
    uint32_t order = 0;
    uint32_t block = PAGE_SIZE;
    while (order < MAX_ORDER && block < size) {
        block <<= 1;
        order++;
    }
    return order;  /* MAX_ORDER (invalid) when size > largest block */
}

/* ---- introspection ------------------------------------------------------- */

int buddy_owns(const void *addr) {
    uintptr_t a = (uintptr_t)addr;
    return buddy_ready && a >= region_start && a < region_end;
}

int buddy_addr_is_managed(uint32_t addr) {
    if (!buddy_ready || addr < region_start || addr >= region_end) {
        return 0;
    }
    uint32_t idx = (addr - region_start) / PAGE_SIZE;
    return page_desc[idx].state != BP_RESERVED;
}

uint32_t buddy_get_managed_pages(void) {
    return buddy_ready ? managed_pages : 0;
}

uint32_t buddy_get_free_page_count(void) {
    return buddy_ready ? free_pages_now : 0;
}

uint32_t buddy_free_blocks_of_order(uint32_t order) {
    if (!buddy_ready || order >= MAX_ORDER) {
        return 0;
    }
    return free_blocks[order];
}

void buddy_get_region(uint32_t *start_addr, uint32_t *end_addr) {
    if (start_addr) {
        *start_addr = buddy_ready ? region_start : 0;
    }
    if (end_addr) {
        *end_addr = buddy_ready ? region_end : 0;
    }
}
