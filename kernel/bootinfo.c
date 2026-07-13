/**
 * TocinOS boot-information consumer (tocinboot_info v1)
 *
 * Validates the TocinBoot register contract (docs/BOOT_PROTOCOL.md §2/§6),
 * copies the info block out of BOOTLOADER memory (spec §3.2: everything the
 * loader hands us is eventually reclaimed), and exposes read-only accessors.
 *
 * Must be initialized before paging is enabled: the 32-bit entry contract
 * guarantees paging off, so all physical addresses (info block, memory map,
 * command line) are directly dereferenceable here.
 */

#include "../include/kernel/bootinfo.h"
#include "../include/kernel/serial.h"

/* Captured by kernel/arch/<arch>/entry.asm before anything clobbers them. */
extern tb_u32 tocinboot_magic_reg;
extern tb_u32 tocinboot_info_ptr;

#define BOOTINFO_CMDLINE_MAX 256

static tocinboot_info info_copy;
static bootinfo_fb_t  fb_info;
static char           cmdline_buf[BOOTINFO_CMDLINE_MAX];
static tb_u64         usable_bytes;
static int            info_valid;
static int            fb_valid;
static int            cmdline_valid;

void bootinfo_init(void) {
    info_valid = 0;
    fb_valid = 0;
    cmdline_valid = 0;
    usable_bytes = 0;

    /* Spec §2: verify BOTH magics before trusting anything else. The info
     * block is additionally guaranteed 4 KiB aligned and below 4 GiB (§3.2). */
    if (tocinboot_magic_reg != TOCINBOOT_REG_MAGIC ||
        tocinboot_info_ptr == 0 ||
        (tocinboot_info_ptr & 0xFFFu) != 0) {
        serial_printf("[BOOT] no tocinboot_info (legacy boot path)\n");
        return;
    }

    const tocinboot_info *raw =
        (const tocinboot_info *)(unsigned long)tocinboot_info_ptr;
    /* Spec §8: a v1 consumer accepts version >= 1; size only ever grows, so
     * anything below our own view is malformed. */
    if (raw->magic != TOCINBOOT_INFO_MAGIC ||
        raw->version < TOCINBOOT_VERSION ||
        raw->size < TOCINBOOT_INFO_SIZE_V1) {
        serial_printf("[BOOT] no tocinboot_info (legacy boot path)\n");
        return;
    }

    /* Stash our v1 view of the block (never read past our own struct, §8.3). */
    {
        const unsigned char *s = (const unsigned char *)raw;
        unsigned char *d = (unsigned char *)&info_copy;
        for (unsigned int i = 0; i < sizeof(tocinboot_info); i++)
            d[i] = s[i];
        /* loader_name is NUL-terminated per spec; enforce it defensively. */
        info_copy.loader_name[sizeof(info_copy.loader_name) - 1] = '\0';
    }
    info_valid = 1;

    /* Total usable RAM: iterate by memmap_entry_size, never sizeof (§8.6). */
    if (info_copy.memmap_addr != 0 &&
        info_copy.memmap_entry_size >= sizeof(tocinboot_mmap_entry)) {
        const unsigned char *p =
            (const unsigned char *)(unsigned long)info_copy.memmap_addr;
        for (tb_u32 i = 0; i < info_copy.memmap_count; i++) {
            const tocinboot_mmap_entry *e = (const tocinboot_mmap_entry *)p;
            if (e->type == TOCINBOOT_MEM_USABLE)
                usable_bytes += e->length;
            p += info_copy.memmap_entry_size;
        }
    }

    if ((info_copy.flags & TOCINBOOT_F_FB) && info_copy.fb_base != 0) {
        fb_info.base        = info_copy.fb_base;
        fb_info.width       = info_copy.fb_width;
        fb_info.height      = info_copy.fb_height;
        fb_info.pitch       = info_copy.fb_pitch;
        fb_info.bpp         = info_copy.fb_bpp;
        fb_info.format      = info_copy.fb_format;
        fb_info.red_size    = info_copy.fb_red_size;
        fb_info.red_shift   = info_copy.fb_red_shift;
        fb_info.green_size  = info_copy.fb_green_size;
        fb_info.green_shift = info_copy.fb_green_shift;
        fb_info.blue_size   = info_copy.fb_blue_size;
        fb_info.blue_shift  = info_copy.fb_blue_shift;
        fb_info.rsvd_size   = info_copy.fb_rsvd_size;
        fb_info.rsvd_shift  = info_copy.fb_rsvd_shift;
        fb_valid = 1;
    }

    /* Copy the command line out of BOOTLOADER memory while it is mapped. */
    if ((info_copy.flags & TOCINBOOT_F_CMDLINE) && info_copy.cmdline_addr != 0) {
        const char *src = (const char *)(unsigned long)info_copy.cmdline_addr;
        tb_u32 n = info_copy.cmdline_len;
        if (n > BOOTINFO_CMDLINE_MAX - 1)
            n = BOOTINFO_CMDLINE_MAX - 1;
        tb_u32 i;
        for (i = 0; i < n && src[i] != '\0'; i++)
            cmdline_buf[i] = src[i];
        cmdline_buf[i] = '\0';
        cmdline_valid = 1;
    }

    /* One-line boot summary on COM1. */
    serial_printf("[BOOT] tocinboot_info v%u: %u memmap entries, %u MB usable, ",
                  info_copy.version, info_copy.memmap_count,
                  (tb_u32)(usable_bytes >> 20));
    if (fb_valid)
        serial_printf("fb %ux%ux%u", fb_info.width, fb_info.height, fb_info.bpp);
    else
        serial_printf("fb none");
    if (cmdline_valid)
        serial_printf(", cmdline \"%s\"", cmdline_buf);
    serial_printf(", loader \"%s\"\n", info_copy.loader_name);
}

int bootinfo_present(void) {
    return info_valid;
}

const tocinboot_info *bootinfo_get(void) {
    return info_valid ? &info_copy : (const tocinboot_info *)0;
}

tb_u64 bootinfo_usable_memory_bytes(void) {
    return info_valid ? usable_bytes : 0;
}

const bootinfo_fb_t *bootinfo_framebuffer(void) {
    return (info_valid && fb_valid) ? &fb_info : (const bootinfo_fb_t *)0;
}

const char *bootinfo_cmdline(void) {
    return (info_valid && cmdline_valid) ? cmdline_buf : (const char *)0;
}
