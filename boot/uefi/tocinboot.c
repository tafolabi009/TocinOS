/*
 * TocinBoot v0.1 — TocinOS UEFI loader (milestone M1 + M2 64-bit handoff).
 *
 * Implements the producer side of docs/BOOT_PROTOCOL.md (tocinboot_info v1):
 *   - loads \EFI\TOCINOS\KERNEL.ELF (plain ELF32 ET_EXEC/EM_386 or ELF64
 *     ET_EXEC/EM_X86_64) at its physical p_paddr addresses, optional
 *     CMDLINE.TXT / INITRD.IMG;
 *   - builds tocinboot_info + a normalized memory map in fresh BOOTLOADER
 *     (EfiLoaderData/Code) pages below 4 GiB;
 *   - ELF32: exits boot services and drops from long mode to paging-off
 *     32-bit protected mode via a relocatable trampoline (handoff32.asm),
 *     entering the kernel per spec §6.1 (EAX=magic, EBX=&info, flat GDT, jmp);
 *   - ELF64 (M2): exits boot services and stays in long mode on the
 *     firmware's identity-mapped page tables (spec §6.3 caveat), entering
 *     the kernel per spec §6.2 (RAX=magic, RDI=&info, RSP=loader stack, jmp).
 *
 * Build: x86_64-w64-mingw32-gcc as a native PE32+ EFI application; see the
 * Makefile in this directory. Freestanding: no headers beyond efi.h and
 * boot/tocinboot.h; memcpy/memset are provided below for the compiler.
 */

#include "efi.h"
#include "boot/tocinboot.h"
#include "handoff32.h" /* generated: static const unsigned char handoff32_blob[] */

/* ---- trampoline blob layout (MUST match handoff32.asm) ------------------ */

#define TB_HANDOFF32_SLOTS_OFF 0xC0u
#define TB_HANDOFF32_SLOT_INFO (TB_HANDOFF32_SLOTS_OFF + 0x00u)
#define TB_HANDOFF32_SLOT_ENTRY (TB_HANDOFF32_SLOTS_OFF + 0x08u)
#define TB_HANDOFF32_SLOT_STACK (TB_HANDOFF32_SLOTS_OFF + 0x10u)
/* slots (3x8) + GDT (3x8) + gdtr (2+8) */
#define TB_HANDOFF32_SIZE (TB_HANDOFF32_SLOTS_OFF + 24u + 24u + 10u)

TB_STATIC_ASSERT(sizeof(handoff32_blob) == TB_HANDOFF32_SIZE,
                 handoff32_blob_size_matches_asm_layout);

/* ---- handoff block geometry --------------------------------------------- */

#define TB_INFO_PAGES 1u                        /* tocinboot_info            */
#define TB_MMAP_PAGES 4u                        /* normalized map array      */
#define TB_STACK_PAGES 4u                       /* 16 KiB handoff stack      */
#define TB_DATA_PAGES (TB_INFO_PAGES + TB_MMAP_PAGES + TB_STACK_PAGES)
#define TB_MMAP_CAP ((TB_MMAP_PAGES * EFI_PAGE_SIZE) / sizeof(tocinboot_mmap_entry))

#define TB_BELOW_4G 0xFFFFFFFFull
#define TB_LOADER_NAME "TocinBoot 0.1 (UEFI)"

/* ---- minimal ELF32 ------------------------------------------------------- */

typedef struct {
    UINT8 e_ident[16];
    UINT16 e_type, e_machine;
    UINT32 e_version, e_entry, e_phoff, e_shoff, e_flags;
    UINT16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    UINT32 p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags,
        p_align;
} Elf32_Phdr;

#define ELFCLASS32 1
#define ELFCLASS64 2
#define ET_EXEC 2
#define EM_386 3
#define EM_X86_64 62
#define PT_LOAD 1

/* ---- minimal ELF64 (M2: 64-bit long-mode handoff, spec §6.2) ------------- */

typedef struct {
    UINT8 e_ident[16];
    UINT16 e_type, e_machine;
    UINT32 e_version;
    UINT64 e_entry, e_phoff, e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    UINT32 p_type, p_flags;
    UINT64 p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;

/* ---- freestanding memcpy/memset (MinGW may emit calls to these) --------- */

void *memcpy(void *dst, const void *src, UINTN n)
{
    UINT8 *d = dst;
    const UINT8 *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, UINTN n)
{
    UINT8 *d = dst;
    while (n--)
        *d++ = (UINT8)c;
    return dst;
}

/* ---- globals -------------------------------------------------------------- */

static EFI_SYSTEM_TABLE *g_st;
static EFI_BOOT_SERVICES *g_bs;

static EFI_GUID g_loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID g_sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID g_file_info_guid = EFI_FILE_INFO_ID;
static EFI_GUID g_gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID g_acpi20_guid = EFI_ACPI_20_TABLE_GUID;
static EFI_GUID g_acpi10_guid = EFI_ACPI_10_TABLE_GUID;

/* ---- output: COM1 (raw port I/O) + ConOut -------------------------------- */

static void io_outb(UINT16 port, UINT8 val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static UINT8 io_inb(UINT16 port)
{
    UINT8 val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void serial_putc(char c)
{
    while (!(io_inb(0x3FD) & 0x20)) /* LSR: THR empty */
        ;
    io_outb(0x3F8, (UINT8)c);
}

static void serial_puts(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s);
    }
}

/* ASCII -> CHAR16 conversion in small chunks; safe only before EBS. */
static void con_puts(const char *s)
{
    CHAR16 buf[64];
    UINTN i = 0;

    if (!g_st || !g_st->ConOut)
        return;
    while (*s) {
        if (i >= 61) {
            buf[i] = 0;
            g_st->ConOut->OutputString(g_st->ConOut, buf);
            i = 0;
        }
        if (*s == '\n')
            buf[i++] = u'\r';
        buf[i++] = (CHAR16)(unsigned char)*s++;
    }
    buf[i] = 0;
    if (i)
        g_st->ConOut->OutputString(g_st->ConOut, buf);
}

/* Pre-EBS log: both sinks. Post-EBS, callers use serial_puts directly. */
static void logs(const char *s)
{
    serial_puts(s);
    con_puts(s);
}

static void fmt_hex64(char *dst, UINT64 v) /* "0x" + 16 digits + NUL */
{
    static const char hexdig[] = "0123456789ABCDEF";
    int i;

    dst[0] = '0';
    dst[1] = 'x';
    for (i = 0; i < 16; i++)
        dst[2 + i] = hexdig[(v >> (60 - 4 * i)) & 0xF];
    dst[18] = 0;
}

static void fmt_dec64(char *dst, UINT64 v) /* up to 20 digits + NUL */
{
    char tmp[20];
    int i = 0, j = 0;

    do {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    while (i)
        dst[j++] = tmp[--i];
    dst[j] = 0;
}

static void log_hex(const char *pre, UINT64 v, const char *post)
{
    char b[19];

    fmt_hex64(b, v);
    logs(pre);
    logs(b);
    logs(post);
}

static void log_dec(const char *pre, UINT64 v, const char *post)
{
    char b[21];

    fmt_dec64(b, v);
    logs(pre);
    logs(b);
    logs(post);
}

static EFI_STATUS fail(const char *msg, EFI_STATUS st)
{
    logs("TocinBoot: ERROR: ");
    logs(msg);
    if (st)
        log_hex(" (status ", st, ")");
    logs("\n");
    if (g_bs)
        g_bs->Stall(3 * 1000 * 1000); /* leave the message readable */
    return EFI_ERROR(st) ? st : EFI_LOAD_ERROR;
}

/* ---- small utilities ------------------------------------------------------ */

static BOOLEAN guid_eq(const EFI_GUID *a, const EFI_GUID *b)
{
    const UINT8 *x = (const UINT8 *)a;
    const UINT8 *y = (const UINT8 *)b;
    UINTN i;

    for (i = 0; i < sizeof(EFI_GUID); i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

static EFI_STATUS alloc_pages_below_4g(EFI_MEMORY_TYPE type, UINTN pages,
                                       EFI_PHYSICAL_ADDRESS *out)
{
    EFI_PHYSICAL_ADDRESS addr = TB_BELOW_4G;
    EFI_STATUS st = g_bs->AllocatePages(AllocateMaxAddress, type, pages, &addr);

    if (!EFI_ERROR(st))
        *out = addr;
    return st;
}

/*
 * Read a whole file from the volume into fresh EfiLoaderData pages below
 * 4 GiB (page-aligned by construction). `extra` bytes of headroom are
 * included in the allocation (used for the cmdline NUL).
 */
static EFI_STATUS load_file(EFI_FILE_PROTOCOL *root, const CHAR16 *path,
                            UINT64 extra, EFI_PHYSICAL_ADDRESS *out_addr,
                            UINT64 *out_size)
{
    EFI_FILE_PROTOCOL *f = 0;
    UINT64 info_buf[64]; /* EFI_FILE_INFO + filename, 8-byte aligned */
    UINTN info_size = sizeof(info_buf);
    EFI_PHYSICAL_ADDRESS addr = 0;
    UINT64 fsize, left;
    UINTN pages;
    UINT8 *p;
    EFI_STATUS st;

    st = root->Open(root, &f, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st))
        return st;

    st = f->GetInfo(f, &g_file_info_guid, &info_size, info_buf);
    if (EFI_ERROR(st)) {
        f->Close(f);
        return st;
    }
    fsize = ((EFI_FILE_INFO *)info_buf)->FileSize;

    pages = (UINTN)((fsize + extra + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE);
    if (!pages)
        pages = 1;
    st = alloc_pages_below_4g(EfiLoaderData, pages, &addr);
    if (EFI_ERROR(st)) {
        f->Close(f);
        return st;
    }

    p = (UINT8 *)(UINTN)addr;
    left = fsize;
    while (left) {
        UINTN chunk = (UINTN)left;

        st = f->Read(f, &chunk, p);
        if (EFI_ERROR(st)) {
            f->Close(f);
            return st;
        }
        if (!chunk)
            break; /* unexpected EOF */
        p += chunk;
        left -= chunk;
    }
    f->Close(f);
    if (left)
        return EFI_LOAD_ERROR;

    *out_addr = addr;
    *out_size = fsize;
    return EFI_SUCCESS;
}

/* ---- GOP -> framebuffer fields (spec §5) ---------------------------------- */

static void mask_to_size_shift(UINT32 mask, tb_u8 *size, tb_u8 *shift)
{
    tb_u8 sh = 0, sz = 0;

    if (!mask) {
        *size = 0;
        *shift = 0;
        return;
    }
    while (!(mask & 1)) {
        mask >>= 1;
        sh++;
    }
    while (mask & 1) {
        mask >>= 1;
        sz++;
    }
    *size = sz;
    *shift = sh;
}

static void fill_framebuffer(tocinboot_info *bi)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi;
    UINT32 fmt, bpp = 32;
    EFI_STATUS st;

    st = g_bs->LocateProtocol(&g_gop_guid, 0, (VOID **)&gop);
    if (EFI_ERROR(st) || !gop || !gop->Mode || !gop->Mode->Info) {
        logs("TocinBoot: no GOP; booting without a framebuffer\n");
        return;
    }
    mi = gop->Mode->Info;

    switch (mi->PixelFormat) {
    case PixelBlueGreenRedReserved8BitPerColor: /* bytes B,G,R,X */
        fmt = TOCINBOOT_FB_XRGB32;
        break;
    case PixelRedGreenBlueReserved8BitPerColor: /* bytes R,G,B,X */
        fmt = TOCINBOOT_FB_XBGR32;
        break;
    case PixelBitMask:
        fmt = TOCINBOOT_FB_BITMASK;
        break;
    default:
        logs("TocinBoot: GOP is Blt-only; no linear framebuffer\n");
        return;
    }

    if (fmt == TOCINBOOT_FB_BITMASK) {
        UINT32 all = mi->PixelInformation.RedMask |
                     mi->PixelInformation.GreenMask |
                     mi->PixelInformation.BlueMask |
                     mi->PixelInformation.ReservedMask;
        UINT32 top = 0;

        while (all) {
            top++;
            all >>= 1;
        }
        bpp = (top + 7) & ~7u;
        if (!bpp)
            bpp = 32;
        mask_to_size_shift(mi->PixelInformation.RedMask, &bi->fb_red_size,
                           &bi->fb_red_shift);
        mask_to_size_shift(mi->PixelInformation.GreenMask, &bi->fb_green_size,
                           &bi->fb_green_shift);
        mask_to_size_shift(mi->PixelInformation.BlueMask, &bi->fb_blue_size,
                           &bi->fb_blue_shift);
        mask_to_size_shift(mi->PixelInformation.ReservedMask,
                           &bi->fb_rsvd_size, &bi->fb_rsvd_shift);
    } else {
        /* Informative redundant masks for the fixed 32-bit formats. */
        bi->fb_red_size = bi->fb_green_size = bi->fb_blue_size =
            bi->fb_rsvd_size = 8;
        bi->fb_green_shift = 8;
        bi->fb_rsvd_shift = 24;
        if (fmt == TOCINBOOT_FB_XRGB32) { /* u32 pixel 0x00RRGGBB */
            bi->fb_red_shift = 16;
            bi->fb_blue_shift = 0;
        } else { /* XBGR32: u32 pixel 0x00BBGGRR */
            bi->fb_red_shift = 0;
            bi->fb_blue_shift = 16;
        }
    }

    bi->fb_base = gop->Mode->FrameBufferBase;
    bi->fb_width = mi->HorizontalResolution;
    bi->fb_height = mi->VerticalResolution;
    bi->fb_pitch = mi->PixelsPerScanLine * (bpp / 8);
    bi->fb_bpp = bpp;
    bi->fb_format = fmt;
    bi->flags |= TOCINBOOT_F_FB;

    log_dec("TocinBoot: GOP framebuffer ", bi->fb_width, "x");
    log_dec("", bi->fb_height, "");
    log_dec(", pitch ", bi->fb_pitch, " B");
    log_dec(", format ", bi->fb_format, "");
    log_hex(", base ", bi->fb_base, "\n");
}

/* ---- ACPI RSDP from the configuration table (spec §7) --------------------- */

static UINT64 find_rsdp(void)
{
    UINT64 rsdp10 = 0, rsdp20 = 0;
    UINTN i;

    for (i = 0; i < g_st->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *ct = &g_st->ConfigurationTable[i];

        if (guid_eq(&ct->VendorGuid, &g_acpi20_guid))
            rsdp20 = (UINT64)(UINTN)ct->VendorTable;
        else if (guid_eq(&ct->VendorGuid, &g_acpi10_guid))
            rsdp10 = (UINT64)(UINTN)ct->VendorTable;
    }
    return rsdp20 ? rsdp20 : rsdp10; /* ACPI 2.0 preferred */
}

/* ---- ELF32 loading (spec §7) ----------------------------------------------- */

static EFI_STATUS elf32_load(const UINT8 *img, UINT64 img_size,
                             tocinboot_info *bi)
{
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)img;
    UINT64 lo = ~0ull, hi = 0;
    EFI_PHYSICAL_ADDRESS span;
    UINT16 i;
    EFI_STATUS st;

    if (eh->e_type != ET_EXEC)
        return fail("kernel is not ET_EXEC", 0);
    if (eh->e_machine != EM_386)
        return fail("ELF32 kernel is not EM_386", 0);
    if (!eh->e_phnum || eh->e_phentsize < sizeof(Elf32_Phdr))
        return fail("kernel has no usable program headers", 0);
    if ((UINT64)eh->e_phoff + (UINT64)eh->e_phnum * eh->e_phentsize > img_size)
        return fail("kernel program headers exceed file size", 0);

    /* Pass 1: validate segments, compute the covering physical span. */
    for (i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr *ph =
            (const Elf32_Phdr *)(img + eh->e_phoff +
                                 (UINTN)i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD || !ph->p_memsz)
            continue;
        if (ph->p_filesz > ph->p_memsz)
            return fail("PT_LOAD filesz > memsz", 0);
        if ((UINT64)ph->p_offset + ph->p_filesz > img_size)
            return fail("PT_LOAD data exceeds file size", 0);
        if (ph->p_paddr < lo)
            lo = ph->p_paddr;
        if ((UINT64)ph->p_paddr + ph->p_memsz > hi)
            hi = (UINT64)ph->p_paddr + ph->p_memsz;
    }
    if (hi <= lo)
        return fail("kernel has no PT_LOAD segments", 0);

    lo &= ~(EFI_PAGE_SIZE - 1);                        /* page-round down */
    hi = (hi + EFI_PAGE_SIZE - 1) & ~(EFI_PAGE_SIZE - 1); /* page-round up */
    if (hi > TB_BELOW_4G + 1)
        return fail("kernel span crosses 4 GiB", 0);

    /*
     * Claim the whole covering span at its exact physical address (spec §7).
     * EfiLoaderData => normalized BOOTLOADER; the kernel's own extent is
     * published via kernel_phys_base/end (reclaim rule, spec §4.2).
     */
    span = lo;
    st = g_bs->AllocatePages(AllocateAddress, EfiLoaderData,
                             (UINTN)((hi - lo) / EFI_PAGE_SIZE), &span);
    if (EFI_ERROR(st)) {
        log_hex("TocinBoot: ERROR: firmware owns part of kernel span [", lo,
                ", ");
        log_hex("", hi, ")");
        log_hex(" — AllocatePages(AllocateAddress) failed with status ", st,
                "\n");
        g_bs->Stall(3 * 1000 * 1000);
        return st;
    }

    /* Pass 2: place every segment, zero the BSS tails. */
    for (i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr *ph =
            (const Elf32_Phdr *)(img + eh->e_phoff +
                                 (UINTN)i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD || !ph->p_memsz)
            continue;
        memcpy((VOID *)(UINTN)ph->p_paddr, img + ph->p_offset, ph->p_filesz);
        memset((UINT8 *)(UINTN)ph->p_paddr + ph->p_filesz, 0,
               ph->p_memsz - ph->p_filesz);
        log_hex("TocinBoot:   PT_LOAD paddr ", ph->p_paddr, "");
        log_hex(" filesz ", ph->p_filesz, "");
        log_hex(" memsz ", ph->p_memsz, " loaded\n");
    }

    bi->kernel_phys_base = lo;
    bi->kernel_phys_end = hi;
    bi->kernel_entry = eh->e_entry;
    return EFI_SUCCESS;
}

/* ---- ELF64 loading (spec §7, M2) -------------------------------------------
 *
 * Same contract as elf32_load: every PT_LOAD is placed at its exact p_paddr,
 * the covering span is claimed with AllocatePages(AllocateAddress), BSS
 * tails are zero-filled, e_entry is a physical address. TocinBoot v0.1
 * keeps the whole span below 4 GiB (spec §3.2 keeps everything the kernel
 * must read below 4 GiB; a higher-half ELF64 kernel is expected to use
 * physical p_paddr values down here and remap itself).
 */

static EFI_STATUS elf64_load(const UINT8 *img, UINT64 img_size,
                             tocinboot_info *bi)
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)img;
    UINT64 lo = ~0ull, hi = 0;
    EFI_PHYSICAL_ADDRESS span;
    UINT16 i;
    EFI_STATUS st;

    if (img_size < sizeof(Elf64_Ehdr))
        return fail("ELF64 kernel smaller than its header", 0);
    if (eh->e_type != ET_EXEC)
        return fail("kernel is not ET_EXEC", 0);
    if (eh->e_machine != EM_X86_64)
        return fail("ELF64 kernel is not EM_X86_64", 0);
    if (!eh->e_phnum || eh->e_phentsize < sizeof(Elf64_Phdr))
        return fail("kernel has no usable program headers", 0);
    if (eh->e_phoff > img_size ||
        (UINT64)eh->e_phnum * eh->e_phentsize > img_size - eh->e_phoff)
        return fail("kernel program headers exceed file size", 0);

    /* Pass 1: validate segments, compute the covering physical span. */
    for (i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph =
            (const Elf64_Phdr *)(img + eh->e_phoff +
                                 (UINTN)i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD || !ph->p_memsz)
            continue;
        if (ph->p_filesz > ph->p_memsz)
            return fail("PT_LOAD filesz > memsz", 0);
        if (ph->p_offset > img_size ||
            ph->p_filesz > img_size - ph->p_offset)
            return fail("PT_LOAD data exceeds file size", 0);
        if (ph->p_memsz > ~0ull - ph->p_paddr)
            return fail("PT_LOAD wraps physical address space", 0);
        if (ph->p_paddr < lo)
            lo = ph->p_paddr;
        if (ph->p_paddr + ph->p_memsz > hi)
            hi = ph->p_paddr + ph->p_memsz;
    }
    if (hi <= lo)
        return fail("kernel has no PT_LOAD segments", 0);

    lo &= ~(UINT64)(EFI_PAGE_SIZE - 1);                   /* page-round down */
    hi = (hi + EFI_PAGE_SIZE - 1) & ~(UINT64)(EFI_PAGE_SIZE - 1); /* round up */
    if (hi > TB_BELOW_4G + 1)
        return fail("kernel span crosses 4 GiB", 0);

    /* Claim the whole covering span at its exact physical address (§7). */
    span = lo;
    st = g_bs->AllocatePages(AllocateAddress, EfiLoaderData,
                             (UINTN)((hi - lo) / EFI_PAGE_SIZE), &span);
    if (EFI_ERROR(st)) {
        log_hex("TocinBoot: ERROR: firmware owns part of kernel span [", lo,
                ", ");
        log_hex("", hi, ")");
        log_hex(" — AllocatePages(AllocateAddress) failed with status ", st,
                "\n");
        g_bs->Stall(3 * 1000 * 1000);
        return st;
    }

    /* Pass 2: place every segment, zero the BSS tails. */
    for (i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph =
            (const Elf64_Phdr *)(img + eh->e_phoff +
                                 (UINTN)i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD || !ph->p_memsz)
            continue;
        memcpy((VOID *)(UINTN)ph->p_paddr, img + (UINTN)ph->p_offset,
               (UINTN)ph->p_filesz);
        memset((UINT8 *)(UINTN)ph->p_paddr + ph->p_filesz, 0,
               (UINTN)(ph->p_memsz - ph->p_filesz));
        log_hex("TocinBoot:   PT_LOAD paddr ", ph->p_paddr, "");
        log_hex(" filesz ", ph->p_filesz, "");
        log_hex(" memsz ", ph->p_memsz, " loaded\n");
    }

    bi->kernel_phys_base = lo;
    bi->kernel_phys_end = hi;
    bi->kernel_entry = eh->e_entry;
    return EFI_SUCCESS;
}

/* ---- memory map normalization (spec §4) ------------------------------------ */

static UINT32 efi_type_to_tocinboot(UINT32 t)
{
    switch (t) {
    case EfiConventionalMemory:
    case EfiBootServicesCode:
    case EfiBootServicesData:
        return TOCINBOOT_MEM_USABLE;
    case EfiLoaderCode:
    case EfiLoaderData:
        return TOCINBOOT_MEM_BOOTLOADER;
    case EfiACPIReclaimMemory:
        return TOCINBOOT_MEM_ACPI_RECLAIMABLE;
    case EfiACPIMemoryNVS:
        return TOCINBOOT_MEM_ACPI_NVS;
    case EfiUnusableMemory:
        return TOCINBOOT_MEM_BAD;
    default: /* runtime services, reserved, MMIO, pal, persistent, unknown */
        return TOCINBOOT_MEM_RESERVED;
    }
}

/*
 * Convert a raw EFI map into sorted, coalesced tocinboot entries.
 * Returns the entry count, or (INTN)-1 if `cap` is too small.
 * Pure memory work — safe between the final GetMemoryMap and EBS.
 */
static INTN mmap_normalize(const UINT8 *raw, UINTN raw_size, UINTN dsize,
                           tocinboot_mmap_entry *out, UINTN cap)
{
    UINTN n = 0, m = 0, off, i;

    for (off = 0; off + dsize <= raw_size; off += dsize) {
        const EFI_MEMORY_DESCRIPTOR *d =
            (const EFI_MEMORY_DESCRIPTOR *)(raw + off);

        if (!d->NumberOfPages)
            continue;
        if (n >= cap)
            return -1;
        out[n].base = d->PhysicalStart;
        out[n].length = d->NumberOfPages * EFI_PAGE_SIZE;
        out[n].type = efi_type_to_tocinboot(d->Type);
        out[n].reserved = 0;
        n++;
    }

    /* Insertion sort by ascending base (firmware order is not guaranteed). */
    for (i = 1; i < n; i++) {
        tocinboot_mmap_entry key = out[i];
        UINTN j = i;

        while (j > 0 && out[j - 1].base > key.base) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }

    /* Coalesce adjacent entries of the same normalized type. */
    for (i = 0; i < n; i++) {
        if (m && out[m - 1].type == out[i].type &&
            out[m - 1].base + out[m - 1].length == out[i].base)
            out[m - 1].length += out[i].length;
        else
            out[m++] = out[i];
    }
    return (INTN)m;
}

/* ---- entry point ------------------------------------------------------------ */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_PHYSICAL_ADDRESS data_block = 0, tramp = 0, kfile = 0, cfile = 0,
                         ifile = 0;
    UINT64 ksize = 0, csize = 0, isize = 0;
    tocinboot_info *bi;
    tocinboot_mmap_entry *mmap_array;
    UINT64 stack_top, rsdp;
    UINT8 *img, *tp;
    VOID *raw_map = 0;
    UINTN raw_cap, map_size, map_key, desc_size;
    UINT32 desc_ver;
    EFI_STATUS st;
    int attempt;
    int is64 = 0;
    INTN nmm = 0;

    g_st = SystemTable;
    g_bs = SystemTable->BootServices;

    logs("TocinBoot v0.1 (UEFI x86-64)\n");

    /* Firmware watchdog off before anything slow (spec §6). */
    g_bs->SetWatchdogTimer(0, 0, 0, 0);

    /*
     * Handoff block, allocated first so its placement never depends on file
     * sizes: 9 data pages (info + normalized map + 16 KiB stack) plus one
     * EfiLoaderCode page for the trampoline — all below 4 GiB, all
     * normalized to BOOTLOADER type per spec §4.1.
     */
    st = alloc_pages_below_4g(EfiLoaderData, TB_DATA_PAGES, &data_block);
    if (EFI_ERROR(st))
        return fail("cannot allocate handoff data block below 4 GiB", st);
    st = alloc_pages_below_4g(EfiLoaderCode, 1, &tramp);
    if (EFI_ERROR(st))
        return fail("cannot allocate trampoline page below 4 GiB", st);

    memset((VOID *)(UINTN)data_block, 0, TB_DATA_PAGES * EFI_PAGE_SIZE);
    bi = (tocinboot_info *)(UINTN)data_block;
    mmap_array = (tocinboot_mmap_entry *)(UINTN)(data_block +
                                                 TB_INFO_PAGES * EFI_PAGE_SIZE);
    stack_top = data_block + (UINT64)TB_DATA_PAGES * EFI_PAGE_SIZE; /* 16-al. */

    bi->magic = TOCINBOOT_INFO_MAGIC;
    bi->version = TOCINBOOT_VERSION;
    bi->size = TOCINBOOT_INFO_SIZE_V1;
    bi->flags = TOCINBOOT_F_UEFI;
    bi->memmap_addr = (UINT64)(UINTN)mmap_array;
    bi->memmap_entry_size = sizeof(tocinboot_mmap_entry);
    memcpy(bi->loader_name, TB_LOADER_NAME, sizeof(TB_LOADER_NAME));

    fill_framebuffer(bi);

    /* Boot volume: LoadedImage -> DeviceHandle -> SimpleFS -> root dir. */
    st = g_bs->HandleProtocol(ImageHandle, &g_loaded_image_guid, (VOID **)&li);
    if (EFI_ERROR(st))
        return fail("LoadedImage protocol unavailable", st);
    st = g_bs->HandleProtocol(li->DeviceHandle, &g_sfs_guid, (VOID **)&sfs);
    if (EFI_ERROR(st))
        return fail("boot volume has no SimpleFileSystem", st);
    st = sfs->OpenVolume(sfs, &root);
    if (EFI_ERROR(st))
        return fail("cannot open boot volume root", st);

    /* Kernel image (required). */
    st = load_file(root, u"\\EFI\\TOCINOS\\KERNEL.ELF", 0, &kfile, &ksize);
    if (EFI_ERROR(st))
        return fail("cannot read \\EFI\\TOCINOS\\KERNEL.ELF", st);
    log_dec("TocinBoot: KERNEL.ELF read, ", ksize, " bytes\n");

    img = (UINT8 *)(UINTN)kfile;
    if (ksize < sizeof(Elf32_Ehdr) || img[0] != 0x7F || img[1] != 'E' ||
        img[2] != 'L' || img[3] != 'F')
        return fail("KERNEL.ELF is not an ELF file", 0);

    if (img[4] == ELFCLASS64) {
        /* M2: 64-bit long-mode handoff (spec §6.2) — no mode drop needed. */
        is64 = 1;
        st = elf64_load(img, ksize, bi);
        if (EFI_ERROR(st))
            return st; /* diagnostics already printed */
    } else if (img[4] == ELFCLASS32) {
        st = elf32_load(img, ksize, bi);
        if (EFI_ERROR(st))
            return st; /* diagnostics already printed */
    } else {
        return fail("KERNEL.ELF has unknown ELF class", 0);
    }

    /* Optional command line (spec §7): strip one trailing CR/LF, NUL-term. */
    st = load_file(root, u"\\EFI\\TOCINOS\\CMDLINE.TXT", 1, &cfile, &csize);
    if (!EFI_ERROR(st)) {
        char *cmd = (char *)(UINTN)cfile;

        if (csize && cmd[csize - 1] == '\n')
            csize--;
        if (csize && cmd[csize - 1] == '\r')
            csize--;
        cmd[csize] = 0;
        bi->cmdline_addr = cfile;
        bi->cmdline_len = (tb_u32)csize;
        bi->flags |= TOCINBOOT_F_CMDLINE;
        log_dec("TocinBoot: CMDLINE.TXT read, ", csize, " bytes\n");
    }

    /* Optional initrd (spec §7): whole file, page-aligned, below 4 GiB. */
    st = load_file(root, u"\\EFI\\TOCINOS\\INITRD.IMG", 0, &ifile, &isize);
    if (!EFI_ERROR(st)) {
        bi->initrd_addr = ifile;
        bi->initrd_size = isize;
        bi->flags |= TOCINBOOT_F_INITRD;
        log_dec("TocinBoot: INITRD.IMG read, ", isize, " bytes\n");
    }

    rsdp = find_rsdp();
    if (rsdp) {
        bi->rsdp_addr = rsdp;
        bi->flags |= TOCINBOOT_F_RSDP;
    }

    /* Trampoline: copy blob, patch the three handoff slots (32-bit entry
     * only — the 64-bit entry stays in long mode, no mode-drop trampoline). */
    if (!is64) {
        tp = (UINT8 *)(UINTN)tramp;
        memcpy(tp, handoff32_blob, sizeof(handoff32_blob));
        *(UINT64 *)(tp + TB_HANDOFF32_SLOT_INFO) = (UINT64)(UINTN)bi;
        *(UINT64 *)(tp + TB_HANDOFF32_SLOT_ENTRY) = bi->kernel_entry;
        *(UINT64 *)(tp + TB_HANDOFF32_SLOT_STACK) = stack_top;
    }

    /* All summaries BEFORE the final GetMemoryMap (spec EBS discipline). */
    log_hex("TocinBoot: kernel span   [", bi->kernel_phys_base, ", ");
    log_hex("", bi->kernel_phys_end, ")");
    log_hex(", entry ", bi->kernel_entry, "\n");
    log_hex("TocinBoot: tocinboot_info ", (UINT64)(UINTN)bi, "");
    log_hex(", memmap array ", bi->memmap_addr, "");
    log_hex(", stack top ", stack_top, "\n");
    log_hex("TocinBoot: trampoline    ", tramp, "");
    log_hex(", RSDP ", bi->rsdp_addr, "\n");
    logs("TocinBoot: exiting boot services\n");

    /*
     * EBS dance: pre-size the raw map buffer with generous slack, then
     * final GetMemoryMap -> normalize -> ExitBootServices with NOTHING else
     * in between (no prints, no allocations). One retry on stale MapKey.
     */
    map_size = 0;
    st = g_bs->GetMemoryMap(&map_size, 0, &map_key, &desc_size, &desc_ver);
    if (st != EFI_BUFFER_TOO_SMALL)
        return fail("GetMemoryMap size probe failed", st);
    raw_cap = map_size + 2 * EFI_PAGE_SIZE; /* slack for EBS-time growth */
    st = g_bs->AllocatePool(EfiLoaderData, raw_cap, &raw_map);
    if (EFI_ERROR(st))
        return fail("cannot allocate raw memory map buffer", st);

    for (attempt = 0; attempt < 2; attempt++) {
        map_size = raw_cap;
        st = g_bs->GetMemoryMap(&map_size, raw_map, &map_key, &desc_size,
                                &desc_ver);
        if (EFI_ERROR(st))
            return fail("final GetMemoryMap failed", st);

        nmm = mmap_normalize(raw_map, map_size, desc_size, mmap_array,
                             TB_MMAP_CAP);
        if (nmm < 0)
            return fail("normalized memory map exceeds reserved pages", 0);
        bi->memmap_count = (tb_u32)nmm;

        st = g_bs->ExitBootServices(ImageHandle, map_key);
        if (!EFI_ERROR(st))
            break;
        /* Stale MapKey (EFI_INVALID_PARAMETER): loop re-fetches the map.
         * No console output here — the firmware console may be half dead. */
    }
    if (EFI_ERROR(st)) {
        serial_puts("TocinBoot: ERROR: ExitBootServices failed twice\n");
        return st;
    }

    /* ---- boot services are GONE: serial only from here ------------------ */

    serial_puts("TocinBoot: ExitBootServices OK, ");
    {
        char b[19];

        fmt_dec64(b, (UINT64)nmm);
        serial_puts(b);
        serial_puts(" memory map entries\n");
        serial_puts(is64 ? "TocinBoot: 64-bit long-mode handoff, jmp "
                         : "TocinBoot: dropping to 32-bit protected mode, jmp ");
        fmt_hex64(b, bi->kernel_entry);
        serial_puts(b);
        serial_puts("\n");
    }

    if (is64) {
        /*
         * 64-bit long-mode entry (spec §6.2): stay on the firmware's
         * identity-mapped page tables (which cover every region in the
         * memory map and the framebuffer; the §6.3 caveat applies —
         * loader/firmware tables may live in USABLE memory). Pin
         * RAX=TOCINBOOT_REG_MAGIC ("a") and RDI=&tocinboot_info ("D",
         * also the SysV first argument), point RSP at the 16-byte-aligned
         * loader stack top, and jmp (not call) to e_entry. Never returns.
         */
        __asm__ volatile("cli\n\t"
                         "movq %[stk], %%rsp\n\t"
                         "jmp *%[ent]"
                         :
                         : [stk] "r"(stack_top), [ent] "r"(bi->kernel_entry),
                           "a"((UINT64)TOCINBOOT_REG_MAGIC),
                           "D"((UINT64)(UINTN)bi)
                         : "memory");
        __builtin_unreachable();
    }

    /* Jump (not call) into the trampoline; it never returns. */
    __asm__ volatile("cli\n\t"
                     "jmp *%0"
                     :
                     : "r"((VOID *)(UINTN)tramp)
                     : "memory");
    __builtin_unreachable();
}
