/*
 * TocinOS boot protocol — tocinboot_info v1
 *
 * Normative encoding of docs/BOOT_PROTOCOL.md. Shared verbatim by:
 *   - the TocinBoot UEFI loader  (x86-64, MS-ABI PE, -ffreestanding)
 *   - the TocinOS kernel         (i386 or x86-64, -ffreestanding)
 *   - the BIOS stage2 C helpers  (future, M1 follow-up)
 *
 * Layout rules (see spec §1/§3): little-endian, naturally aligned, no
 * implicit padding by construction — every field offset is a multiple of the
 * field size and all padding is explicit reserved fields. Because of that the
 * struct needs no packed attribute and has the identical layout on i386 and
 * x86-64; the static assertions below pin every offset and the total size.
 *
 * Freestanding-safe: includes nothing. Fixed-width types are self-defined
 * (i386 and x86-64 GCC/Clang/MinGW: char=1, short=2, int=4, long long=8).
 */

#ifndef TOCINOS_BOOT_TOCINBOOT_H
#define TOCINOS_BOOT_TOCINBOOT_H

typedef unsigned char      tb_u8;
typedef unsigned short     tb_u16;
typedef unsigned int       tb_u32;
typedef unsigned long long tb_u64;

/* ---- static assertion shim (C11 / C++11 / fallback) -------------------- */
#if defined(__cplusplus)
#define TB_STATIC_ASSERT(cond, tag) static_assert(cond, #tag)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define TB_STATIC_ASSERT(cond, tag) _Static_assert(cond, #tag)
#else
#define TB_STATIC_ASSERT(cond, tag) \
    typedef char tb_static_assert_##tag[(cond) ? 1 : -1]
#endif

TB_STATIC_ASSERT(sizeof(tb_u8) == 1, tb_u8_size);
TB_STATIC_ASSERT(sizeof(tb_u16) == 2, tb_u16_size);
TB_STATIC_ASSERT(sizeof(tb_u32) == 4, tb_u32_size);
TB_STATIC_ASSERT(sizeof(tb_u64) == 8, tb_u64_size);

/* ---- magics (spec §2) --------------------------------------------------- */

/* tocinboot_info.magic: bytes "TBI1" in memory, read as little-endian u32. */
#define TOCINBOOT_INFO_MAGIC 0x31494254u
/* EAX (32-bit entry) / RAX (64-bit entry) at handoff. */
#define TOCINBOOT_REG_MAGIC  0x70C1B007u

#define TOCINBOOT_VERSION 1u
#define TOCINBOOT_INFO_SIZE_V1 232u

/* ---- flags (spec §3.1) -------------------------------------------------- */

#define TOCINBOOT_F_UEFI    (1u << 0)
#define TOCINBOOT_F_BIOS    (1u << 1)
#define TOCINBOOT_F_FB      (1u << 2)
#define TOCINBOOT_F_RSDP    (1u << 3)
#define TOCINBOOT_F_CMDLINE (1u << 4)
#define TOCINBOOT_F_INITRD  (1u << 5)

/* ---- normalized memory map types (spec §4.1) ---------------------------- */

#define TOCINBOOT_MEM_INVALID          0u /* never emitted */
#define TOCINBOOT_MEM_USABLE           1u
#define TOCINBOOT_MEM_RESERVED         2u
#define TOCINBOOT_MEM_ACPI_RECLAIMABLE 3u
#define TOCINBOOT_MEM_ACPI_NVS         4u
#define TOCINBOOT_MEM_BAD              5u
#define TOCINBOOT_MEM_BOOTLOADER       6u /* reclaim per spec §4.2 */
#define TOCINBOOT_MEM_KERNEL_MODULES   7u
#define TOCINBOOT_MEM_FRAMEBUFFER      8u

/* ---- framebuffer pixel formats (spec §5.1) ------------------------------ */

#define TOCINBOOT_FB_NONE    0u
#define TOCINBOOT_FB_XRGB32  1u /* u32 pixel 0x00RRGGBB; bytes B,G,R,X */
#define TOCINBOOT_FB_XBGR32  2u /* u32 pixel 0x00BBGGRR; bytes R,G,B,X */
#define TOCINBOOT_FB_BITMASK 3u /* use fb_*_size/shift */

/* ---- memory map entry (spec §4): 24 bytes, iterate by memmap_entry_size - */

typedef struct tocinboot_mmap_entry {
    tb_u64 base;     /* 0x00 physical start */
    tb_u64 length;   /* 0x08 bytes, never 0 */
    tb_u32 type;     /* 0x10 TOCINBOOT_MEM_* */
    tb_u32 reserved; /* 0x14 MUST be 0 */
} tocinboot_mmap_entry;

TB_STATIC_ASSERT(sizeof(tocinboot_mmap_entry) == 24, mmap_entry_size);

/* ---- tocinboot_info (spec §3): 232 bytes -------------------------------- */

typedef struct tocinboot_info {
    tb_u32 magic;             /* 0x00 TOCINBOOT_INFO_MAGIC          */
    tb_u32 version;           /* 0x04 TOCINBOOT_VERSION             */
    tb_u32 size;              /* 0x08 bytes written by the loader   */
    tb_u32 flags;             /* 0x0C TOCINBOOT_F_*                 */

    tb_u64 memmap_addr;       /* 0x10 phys addr of mmap entries     */
    tb_u32 memmap_count;      /* 0x18                               */
    tb_u32 memmap_entry_size; /* 0x1C stride in bytes (v1: 24)      */

    tb_u64 fb_base;           /* 0x20 phys framebuffer base         */
    tb_u32 fb_width;          /* 0x28 visible pixels per row        */
    tb_u32 fb_height;         /* 0x2C visible rows                  */
    tb_u32 fb_pitch;          /* 0x30 BYTES per row                 */
    tb_u32 fb_bpp;            /* 0x34 bits per pixel                */
    tb_u32 fb_format;         /* 0x38 TOCINBOOT_FB_*                */
    tb_u8  fb_red_size;       /* 0x3C */
    tb_u8  fb_red_shift;      /* 0x3D */
    tb_u8  fb_green_size;     /* 0x3E */
    tb_u8  fb_green_shift;    /* 0x3F */
    tb_u8  fb_blue_size;      /* 0x40 */
    tb_u8  fb_blue_shift;     /* 0x41 */
    tb_u8  fb_rsvd_size;      /* 0x42 */
    tb_u8  fb_rsvd_shift;     /* 0x43 */
    tb_u32 reserved0;         /* 0x44 MUST be 0                     */

    tb_u64 rsdp_addr;         /* 0x48 ACPI RSDP phys addr, 0 = none */
    tb_u64 cmdline_addr;      /* 0x50 phys addr, 0 = none           */
    tb_u32 cmdline_len;       /* 0x58 bytes excluding NUL           */
    tb_u32 reserved1;         /* 0x5C MUST be 0                     */

    tb_u64 initrd_addr;       /* 0x60 phys addr, 0 = none           */
    tb_u64 initrd_size;       /* 0x68 bytes                         */

    tb_u64 kernel_phys_base;  /* 0x70 page-rounded down             */
    tb_u64 kernel_phys_end;   /* 0x78 one past end, page-rounded up */
    tb_u64 kernel_entry;      /* 0x80 e_entry the loader jumped to  */

    char   loader_name[32];   /* 0x88 NUL-terminated ASCII          */

    tb_u64 reserved2[8];      /* 0xA8 MUST be 0                     */
} tocinboot_info;             /* 0xE8 = 232 total                   */

TB_STATIC_ASSERT(sizeof(tocinboot_info) == TOCINBOOT_INFO_SIZE_V1, info_size);

#if defined(__GNUC__) || defined(__clang__)
#define TB_OFF(f) __builtin_offsetof(tocinboot_info, f)
TB_STATIC_ASSERT(TB_OFF(magic) == 0x00, off_magic);
TB_STATIC_ASSERT(TB_OFF(version) == 0x04, off_version);
TB_STATIC_ASSERT(TB_OFF(size) == 0x08, off_size);
TB_STATIC_ASSERT(TB_OFF(flags) == 0x0C, off_flags);
TB_STATIC_ASSERT(TB_OFF(memmap_addr) == 0x10, off_memmap_addr);
TB_STATIC_ASSERT(TB_OFF(memmap_count) == 0x18, off_memmap_count);
TB_STATIC_ASSERT(TB_OFF(memmap_entry_size) == 0x1C, off_memmap_entry_size);
TB_STATIC_ASSERT(TB_OFF(fb_base) == 0x20, off_fb_base);
TB_STATIC_ASSERT(TB_OFF(fb_width) == 0x28, off_fb_width);
TB_STATIC_ASSERT(TB_OFF(fb_height) == 0x2C, off_fb_height);
TB_STATIC_ASSERT(TB_OFF(fb_pitch) == 0x30, off_fb_pitch);
TB_STATIC_ASSERT(TB_OFF(fb_bpp) == 0x34, off_fb_bpp);
TB_STATIC_ASSERT(TB_OFF(fb_format) == 0x38, off_fb_format);
TB_STATIC_ASSERT(TB_OFF(fb_red_size) == 0x3C, off_fb_red_size);
TB_STATIC_ASSERT(TB_OFF(reserved0) == 0x44, off_reserved0);
TB_STATIC_ASSERT(TB_OFF(rsdp_addr) == 0x48, off_rsdp_addr);
TB_STATIC_ASSERT(TB_OFF(cmdline_addr) == 0x50, off_cmdline_addr);
TB_STATIC_ASSERT(TB_OFF(cmdline_len) == 0x58, off_cmdline_len);
TB_STATIC_ASSERT(TB_OFF(initrd_addr) == 0x60, off_initrd_addr);
TB_STATIC_ASSERT(TB_OFF(initrd_size) == 0x68, off_initrd_size);
TB_STATIC_ASSERT(TB_OFF(kernel_phys_base) == 0x70, off_kernel_phys_base);
TB_STATIC_ASSERT(TB_OFF(kernel_phys_end) == 0x78, off_kernel_phys_end);
TB_STATIC_ASSERT(TB_OFF(kernel_entry) == 0x80, off_kernel_entry);
TB_STATIC_ASSERT(TB_OFF(loader_name) == 0x88, off_loader_name);
TB_STATIC_ASSERT(TB_OFF(reserved2) == 0xA8, off_reserved2);
#undef TB_OFF
#endif /* __GNUC__ || __clang__ */

#endif /* TOCINOS_BOOT_TOCINBOOT_H */
