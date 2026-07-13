/**
 * TocinOS boot-information consumer (tocinboot_info v1)
 *
 * Kernel-side view of the TocinBoot handoff (docs/BOOT_PROTOCOL.md).
 * kernel/arch/<arch>/entry.asm captures the entry registers into
 * tocinboot_magic_reg / tocinboot_info_ptr; bootinfo_init() validates both
 * magics per spec §2 and, when valid, stashes a copy of the info block.
 *
 * On legacy boot paths (BIOS MBR+stage2, multiboot QEMU -kernel) the magics
 * do not match: bootinfo_present() returns 0 and every accessor degrades to
 * "nothing available" — the kernel behaves exactly as before.
 */

#ifndef KERNEL_BOOTINFO_H
#define KERNEL_BOOTINFO_H

#include "../boot/tocinboot.h"

/** Framebuffer description lifted from tocinboot_info (spec §5). */
typedef struct bootinfo_fb {
    tb_u64 base;        /* physical framebuffer base                  */
    tb_u32 width;       /* visible pixels per row                     */
    tb_u32 height;      /* visible rows                               */
    tb_u32 pitch;       /* BYTES per row                              */
    tb_u32 bpp;         /* bits per pixel                             */
    tb_u32 format;      /* TOCINBOOT_FB_*                             */
    tb_u8  red_size, red_shift;
    tb_u8  green_size, green_shift;
    tb_u8  blue_size, blue_shift;
    tb_u8  rsvd_size, rsvd_shift;
} bootinfo_fb_t;

/**
 * Validate and consume the TocinBoot handoff. Call once, immediately after
 * serial_init(), while physical addresses are still directly dereferenceable
 * (32-bit entry: paging off, spec §6.1). Prints exactly one serial line:
 * either a "[BOOT] tocinboot_info ..." summary or
 * "[BOOT] no tocinboot_info (legacy boot path)".
 */
void bootinfo_init(void);

/** 1 if a valid tocinboot_info was handed off, 0 on legacy boot paths. */
int bootinfo_present(void);

/** Stashed copy of the info block, or (void*)0 when !bootinfo_present(). */
const tocinboot_info *bootinfo_get(void);

/** Sum of all USABLE memmap region lengths in bytes (0 if not present). */
tb_u64 bootinfo_usable_memory_bytes(void);

/** GOP/VBE framebuffer description, or (void*)0 if none was handed off. */
const bootinfo_fb_t *bootinfo_framebuffer(void);

/** NUL-terminated kernel command line, or (void*)0 if none. */
const char *bootinfo_cmdline(void);

#endif /* KERNEL_BOOTINFO_H */
