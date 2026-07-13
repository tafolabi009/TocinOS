/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Unit tests for pmm_init_from_bootinfo() in the REAL physical memory
 * manager (kernel/mm/pmm.c), compiled directly into the test runner — no
 * mocks. A fake tocinboot_info + memory map (docs/BOOT_PROTOCOL.md) is
 * crafted host-side and fed to the real code.
 *
 * Semantics under test (conservative v1):
 *   - plain pmm_init() baseline preserved: 32768 pages, first 2 MiB reserved
 *   - every non-USABLE memmap region inside the 128 MiB window is reserved
 *     (BOOTLOADER included — reclaim is M2), iterated by memmap_entry_size
 *   - the framebuffer range is reserved when handed off, without
 *     double-counting pages already reserved by a FRAMEBUFFER memmap entry
 *   - regions beyond 128 MiB are ignored; straddlers are clamped
 */

#include <string.h>

#include "../framework/unittest.h"
#include "../../include/kernel/memory.h"

#define TOTAL_PAGES    32768u          /* 128 MiB / 4 KiB */
#define RESERVED_PAGES 512u            /* first 2 MiB */
#define FIRST_FREE     0x200000u       /* first allocatable address */

/* Spec §8.6: consumers iterate by memmap_entry_size, never sizeof. Use a
 * 32-byte stride (24-byte v1 entry + 8 pad) to prove that. */
typedef struct {
    tocinboot_mmap_entry e;
    tb_u64 pad;
} stride32_entry;

/* Sorted, non-overlapping, coalesced — per spec §4 guarantees. */
static const stride32_entry fake_map[] = {
    { { 0x00000000u, 0x0009F000u, TOCINBOOT_MEM_USABLE,      0 }, 0 },
    { { 0x0009F000u, 0x00061000u, TOCINBOOT_MEM_RESERVED,    0 }, 0 }, /* inside low 2MB: +0  */
    { { 0x00100000u, 0x00200000u, TOCINBOOT_MEM_USABLE,      0 }, 0 },
    { { 0x00300000u, 0x00100000u, TOCINBOOT_MEM_ACPI_NVS,    0 }, 0 }, /* +256 pages          */
    { { 0x00400000u, 0x00100000u, TOCINBOOT_MEM_USABLE,      0 }, 0 },
    { { 0x00500000u, 0x00050000u, TOCINBOOT_MEM_BOOTLOADER,  0 }, 0 }, /* +80 pages (no M2 reclaim) */
    { { 0x00550000u, 0x079B0000u, TOCINBOOT_MEM_USABLE,      0 }, 0 },
    { { 0x07F00000u, 0x00010000u, TOCINBOOT_MEM_FRAMEBUFFER, 0 }, 0 }, /* +16 pages           */
    { { 0x07F10000u, 0x000EF000u, TOCINBOOT_MEM_USABLE,      0 }, 0 },
    { { 0x07FFF000u, 0x00002000u, TOCINBOOT_MEM_RESERVED,    0 }, 0 }, /* straddles 128MB: +1 */
    { { 0x10000000u, 0x10000000u, TOCINBOOT_MEM_USABLE,      0 }, 0 }, /* above window: +0    */
};
#define FAKE_MAP_COUNT (sizeof(fake_map) / sizeof(fake_map[0]))

/* 256 + 80 + 16 + 1 reserved pages on top of the low-2MB baseline. The fb
 * range below equals the FRAMEBUFFER entry, so it must NOT count twice. */
#define EXTRA_RESERVED 353u
#define EXPECT_USED    (RESERVED_PAGES + EXTRA_RESERVED)

static tocinboot_info make_fake_info(void) {
    tocinboot_info info;
    memset(&info, 0, sizeof(info));
    info.magic = TOCINBOOT_INFO_MAGIC;
    info.version = TOCINBOOT_VERSION;
    info.size = TOCINBOOT_INFO_SIZE_V1;
    info.flags = TOCINBOOT_F_UEFI | TOCINBOOT_F_FB;
    info.memmap_addr = (tb_u64)(uintptr_t)fake_map;
    info.memmap_count = FAKE_MAP_COUNT;
    info.memmap_entry_size = sizeof(stride32_entry);
    /* Framebuffer: same 16 pages the FRAMEBUFFER memmap entry covers. */
    info.fb_base = 0x07F00000u;
    info.fb_width = 1024;
    info.fb_height = 16;
    info.fb_pitch = 4096;
    info.fb_bpp = 32;
    info.fb_format = TOCINBOOT_FB_XRGB32;
    return info;
}

static int in_range(unsigned int addr, unsigned int lo, unsigned int hi) {
    return addr >= lo && addr < hi;
}

TEST_SUITE(pmm_bootinfo_tests)

    // Counters add up: baseline + non-USABLE regions + fb, no double count
    TEST_CASE(bootinfo_counters)
        ASSERT_EQ(sizeof(stride32_entry), 32u, "test stride entry must be 32 bytes");

        tocinboot_info info = make_fake_info();
        pmm_init_from_bootinfo(&info);

        ASSERT_EQ(pmm_get_total_pages(), TOTAL_PAGES, "Window stays 128MB / 32768 pages");
        ASSERT_EQ(pmm_get_used_pages(), EXPECT_USED,
                  "Used = 2MB baseline + reserved/NVS/bootloader/fb/clamped straddler");
        ASSERT_EQ(pmm_get_free_pages(), TOTAL_PAGES - EXPECT_USED, "Free = total - used");

        unsigned int first = pmm_alloc_page();
        ASSERT_EQ(first, FIRST_FREE, "First allocation still starts at 2MB");
    END_TEST_CASE()

    // Reserved regions are never handed out; everything usable is
    TEST_CASE(bootinfo_reserved_not_allocatable)
        static unsigned int allocated[TOTAL_PAGES];
        tocinboot_info info = make_fake_info();
        pmm_init_from_bootinfo(&info);

        unsigned int expect_free = pmm_get_free_pages();
        unsigned int count = 0;
        unsigned int bad = 0;
        unsigned int last = 0;

        for (;;) {
            unsigned int page = pmm_alloc_page();
            if (page == 0)
                break;
            allocated[count++] = page;
            last = page;
            if (in_range(page, 0x00300000u, 0x00400000u) ||  /* ACPI_NVS    */
                in_range(page, 0x00500000u, 0x00550000u) ||  /* BOOTLOADER  */
                in_range(page, 0x07F00000u, 0x07F10000u) ||  /* fb range    */
                in_range(page, 0x07FFF000u, 0x08000000u) ||  /* straddler   */
                page < FIRST_FREE)                           /* low 2MB     */
                bad++;
        }

        ASSERT_EQ(bad, 0u, "No allocation may land in a reserved region");
        ASSERT_EQ(count, expect_free, "Exactly the free page count is allocatable");
        ASSERT_EQ(count, TOTAL_PAGES - EXPECT_USED, "Free count matches the crafted map");
        ASSERT_EQ(last, 0x07FFE000u, "Top allocatable page sits below the clamped straddler");
        ASSERT_EQ(pmm_alloc_page(), 0u, "Exhausted allocator returns 0");

        for (unsigned int i = 0; i < count; i++) {
            pmm_free_page(allocated[i]);
        }
        ASSERT_EQ(pmm_get_free_pages(), expect_free, "All pages freed again");
    END_TEST_CASE()

    // NULL info degrades to plain pmm_init() behavior
    TEST_CASE(bootinfo_null_fallback)
        pmm_init_from_bootinfo((const tocinboot_info *)0);

        ASSERT_EQ(pmm_get_total_pages(), TOTAL_PAGES, "128MB should be 32768 pages");
        ASSERT_EQ(pmm_get_used_pages(), RESERVED_PAGES, "Only the first 2MB reserved");
        ASSERT_EQ(pmm_alloc_page(), FIRST_FREE, "First allocation starts after reserved 2MB");
    END_TEST_CASE()

END_TEST_SUITE()
