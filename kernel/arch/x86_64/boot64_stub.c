/*
 * TocinOS M2 bring-up: minimal 64-bit stub kernel (boot-to-serial proof).
 *
 * Consumer side of the TocinBoot 64-bit long-mode handoff
 * (docs/BOOT_PROTOCOL.md §6.2). Entered from entry64_stub.asm with
 * arg0 = tocinboot_info physical pointer (was RDI) and arg1 = the RAX
 * register magic. It validates both protocol magics, walks the memory map
 * with the spec-mandated memmap_entry_size stride (§8 rule 6), prints the
 * proof banner on COM1 and returns to the entry stub, which halts.
 *
 * §6.3 caveat compliance: the stub only writes its own .data/.bss (inside
 * the loader-allocated BOOTLOADER kernel span) and I/O ports — it never
 * writes USABLE memory, so running on the loader's page tables is safe.
 *
 * Freestanding: includes only the normative protocol header. Built by the
 * root Makefile target `kernel64-stub`; not part of the ARCH=x86 kernel.
 */

#include "boot/tocinboot.h"

#define COM1_BASE 0x3F8

static void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static unsigned char inb(unsigned short port)
{
    unsigned char val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void serial_init(void)
{
    outb(COM1_BASE + 1, 0x00); /* disable UART interrupts            */
    outb(COM1_BASE + 3, 0x80); /* DLAB on                            */
    outb(COM1_BASE + 0, 0x01); /* divisor 1 -> 115200 baud           */
    outb(COM1_BASE + 1, 0x00);
    outb(COM1_BASE + 3, 0x03); /* 8N1, DLAB off                      */
    outb(COM1_BASE + 2, 0xC7); /* FIFO on, clear, 14-byte threshold  */
    outb(COM1_BASE + 4, 0x03); /* DTR | RTS                          */
}

static void serial_putc(char c)
{
    while (!(inb(COM1_BASE + 5) & 0x20)) /* LSR: THR empty */
        ;
    outb(COM1_BASE, (unsigned char)c);
}

static void serial_puts(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s);
    }
}

static void serial_putdec(tb_u64 v)
{
    char tmp[20];
    int i = 0;

    do {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    while (i)
        serial_putc(tmp[--i]);
}

static void serial_puthex(tb_u64 v)
{
    static const char hexdig[] = "0123456789ABCDEF";
    int i;

    serial_puts("0x");
    for (i = 60; i >= 0; i -= 4)
        serial_putc(hexdig[(v >> i) & 0xF]);
}

void stub64_main(const tocinboot_info *bi, tb_u64 reg_magic)
{
    tb_u64 usable = 0;
    tb_u32 i, n = 0;
    const tb_u8 *mm;

    serial_init();
    serial_puts("stub64: entered 64-bit long mode via TocinBoot\n");

    if (reg_magic != (tb_u64)TOCINBOOT_REG_MAGIC) {
        serial_puts("stub64: FAIL: RAX register magic mismatch: ");
        serial_puthex(reg_magic);
        serial_puts("\n");
        return;
    }
    if (!bi) {
        serial_puts("stub64: FAIL: RDI is NULL, no tocinboot_info\n");
        return;
    }
    if (bi->magic != TOCINBOOT_INFO_MAGIC) {
        serial_puts("stub64: FAIL: tocinboot_info magic mismatch: ");
        serial_puthex(bi->magic);
        serial_puts("\n");
        return;
    }
    if (bi->version < TOCINBOOT_VERSION || bi->size < TOCINBOOT_INFO_SIZE_V1) {
        serial_puts("stub64: FAIL: tocinboot_info version/size too small\n");
        return;
    }
    if (!bi->memmap_addr || !bi->memmap_entry_size) {
        serial_puts("stub64: FAIL: tocinboot_info has no memory map\n");
        return;
    }

    /* Walk the map with the memmap_entry_size stride (spec §8 rule 6). */
    mm = (const tb_u8 *)(unsigned long)bi->memmap_addr;
    for (i = 0; i < bi->memmap_count; i++) {
        const tocinboot_mmap_entry *e =
            (const tocinboot_mmap_entry *)(mm +
                                           (tb_u64)i * bi->memmap_entry_size);

        n++;
        if (e->type == TOCINBOOT_MEM_USABLE)
            usable += e->length;
    }

    serial_puts("=== TocinOS 64-bit stub: tocinboot_info OK (");
    serial_putdec(n);
    serial_puts(" memmap entries) ===\n");

    serial_puts("stub64: loader '");
    serial_puts(bi->loader_name);
    serial_puts("', usable memory ");
    serial_putdec(usable >> 20);
    serial_puts(" MiB\n");

    serial_puts("stub64: kernel span [");
    serial_puthex(bi->kernel_phys_base);
    serial_puts(", ");
    serial_puthex(bi->kernel_phys_end);
    serial_puts("), entry ");
    serial_puthex(bi->kernel_entry);
    serial_puts("\n");

    serial_puts("stub64: halting\n");
}
