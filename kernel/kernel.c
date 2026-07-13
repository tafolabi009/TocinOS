/**
 * TocinOS Kernel Main Entry Point
 * 
 * This file contains the main kernel initialization and execution logic
 */

#include "../include/kernel/kernel.h"
#include "../include/kernel/bootinfo.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/task.h"
#include "../include/kernel/cpu_info.h"
#include "../include/kernel/gdt.h"
#include "../include/kernel/idt.h"
#include "../include/kernel/isr.h"
#include "../include/kernel/timer.h"
#include "../include/kernel/keyboard.h"
#include "../include/kernel/serial.h"
#include "../include/kernel/syscall.h"
#include "../include/kernel/shell.h"
#include "../include/kernel/usermode.h"
#include "../include/kernel/process.h"
#include "../include/kernel/fat.h"
#include "../include/kernel/ext2.h"
#include "../include/kernel/vfs.h"
#include "../include/kernel/elf.h"
#include "../include/kernel/procfs.h"
#include "../include/kernel/devfs.h"
#include "../include/kernel/tmpfs.h"
#include "../include/drivers/mdf.h"
#include "../include/drivers/vesa.h"
#include "../include/drivers/fbcon.h"
#include "../include/drivers/ide.h"
#include "../include/drivers/net.h"
#include "../include/drivers/usb_core.h"

// VGA text mode buffer
#define VGA_MEMORY 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile unsigned short *vga_buffer = (unsigned short *)VGA_MEMORY;
static int vga_x = 0;
static int vga_y = 0;

// Boot identity-map page tables for [4MB, 128MB) (PDE 1..31). These MUST
// be static kernel memory: they are installed right after vmm_init(),
// when the only mapped RAM is the low 4MB — allocating them from the PMM
// could return a frame above 4MB, whose zero-fill would fault before the
// IDT even exists (observed as a triple fault on the TocinBoot BIOS path,
// where the memmap leaves no free frames below 4MB). BSS is covered by
// the kernel-footprint reservation below, so the PMM can never hand these
// pages out.
#define BOOT_IDENTITY_TABLES 31  /* PDE 1..31 => [4MB, 128MB) */
static uint32_t boot_identity_tables[BOOT_IDENTITY_TABLES][1024]
    __attribute__((aligned(4096)));

/**
 * Clear the screen
 */
void screen_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (0x0F << 8) | ' ';
    }
    vga_x = 0;
    vga_y = 0;
}

/**
 * Print a string to the screen
 */
void kernel_print(const char *str) {
    // Mirror every kernel_print() line (the '[*]' init log included) onto
    // the framebuffer splash console when it is active. This single hook
    // is the cheapest way to mirror the boot log; before fbcon_init()
    // runs, fbcon_active() is 0 and this is a no-op. Serial output is not
    // routed through here, so serial logs are unaffected.
    if (fbcon_active()) {
        fbcon_puts(str);
    }
    while (*str) {
        if (*str == '\n') {
            vga_x = 0;
            vga_y++;
        } else if (*str == '\b') {
            // Backspace
            if (vga_x > 0) {
                vga_x--;
                int offset = vga_y * VGA_WIDTH + vga_x;
                vga_buffer[offset] = (0x0F << 8) | ' ';
            }
        } else {
            int offset = vga_y * VGA_WIDTH + vga_x;
            vga_buffer[offset] = (0x0F << 8) | *str;
            vga_x++;
            if (vga_x >= VGA_WIDTH) {
                vga_x = 0;
                vga_y++;
            }
        }
        if (vga_y >= VGA_HEIGHT) {
            // Scroll screen
            for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
                vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
            }
            // Clear last line
            for (int i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++) {
                vga_buffer[i] = (0x0F << 8) | ' ';
            }
            vga_y = VGA_HEIGHT - 1;
        }
        str++;
    }
}

/**
 * Print a single character to the screen
 */
void kernel_print_char(char c) {
    char str[2] = {c, 0};
    kernel_print(str);
    serial_write(COM1, str);
}

/**
 * Main kernel entry point
 */
void kernel_main(void) {
    // Initialize serial port FIRST for debugging
    serial_init(COM1);
    serial_printf("\n\n=== TocinOS Booting ===\n");
    serial_printf("[EARLY] Serial initialized\n");

    // Consume the TocinBoot handoff (if any) while paging is still off and
    // every physical address in tocinboot_info is directly dereferenceable.
    bootinfo_init();

    screen_clear();
    
    kernel_print("TocinOS v2.0\n");
    serial_printf("[KERNEL] VGA cleared, starting init...\n");
    kernel_print("=============\n\n");
    
    // Detect CPU features
    serial_printf("[INIT] Detecting CPU...\n");
    kernel_print("[*] Detecting CPU features...\n");
    cpu_detect();
    cpu_print_info();
    kernel_print("\n");
    
    // Initialize memory management
    serial_printf("[INIT] PMM init...\n");
    kernel_print("[*] Initializing Physical Memory Manager...\n");
    if (bootinfo_present()) {
        // Same 128MB bitmap, plus reservations from the TocinBoot memory map.
        pmm_init_from_bootinfo(bootinfo_get());
        serial_printf("[PMM] bootinfo memmap applied: %u/%u pages reserved\n",
                      pmm_get_used_pages(), pmm_get_total_pages());
    } else {
        pmm_init();
    }

    // Kernel-footprint guard: pmm_init() reserves only [0, 2MB), but the
    // kernel's BSS now extends past 2MB (the buddy descriptor table and
    // driver arrays live there; end currently ~3.3MB, no _end symbol is
    // exported by linker_x86.ld to measure it exactly). Without this,
    // the very first pmm_alloc_page() would hand out frames overlaying
    // live kernel data. Reserve a conservative window up to 3.5MB; the
    // identity-mapped PMM pool continues at [3.5MB, 16MB).
    #define KERNEL_FOOTPRINT_END 0x380000u  /* 3.5MB, BSS ends ~0x347000 */
    for (unsigned int pg = (2 * 1024 * 1024) / PAGE_SIZE;
         pg < KERNEL_FOOTPRINT_END / PAGE_SIZE; pg++) {
        pmm_set_page_used(pg);
    }

    // Buddy allocator (M2) — physical ownership split (see memory.h):
    // the PMM bitmap keeps [0, 16MB) for legacy single-page users (page
    // tables, user frames, task stacks); the buddy owns [16MB, top) for
    // page-range allocations and the slab/kmalloc backing store. This
    // must run BEFORE vmm_init(): the bootinfo memmap entries live at
    // their original physical address, which is only dereferenceable
    // while paging is still off (same rule as pmm_init_from_bootinfo).
    // The buddy itself never touches the memory it manages, so managing
    // still-unmapped pages here is safe.
    serial_printf("[INIT] Buddy init...\n");
    kernel_print("[*] Initializing Buddy Allocator...\n");
    buddy_init_from_bootinfo(bootinfo_present() ? bootinfo_get()
                                                : (const tocinboot_info *)0);
    {
        // Mark every buddy-managed page as used in the PMM bitmap so the
        // two allocators can never hand out the same frame. Pages the
        // buddy skipped (bootinfo holes, framebuffer) were already
        // reserved by pmm_init_from_bootinfo, so the counters stay exact.
        uint32_t bstart, bend;
        buddy_get_region(&bstart, &bend);
        for (uint32_t addr = bstart; addr < bend; addr += PAGE_SIZE) {
            if (buddy_addr_is_managed(addr)) {
                pmm_set_page_used(addr / PAGE_SIZE);
            }
        }
    }
    serial_printf("[BUDDY] managing %u MB in %u orders\n",
                  buddy_get_managed_pages() / 256u, (uint32_t)MAX_ORDER);

    serial_printf("[INIT] VMM init...\n");
    kernel_print("[*] Initializing Virtual Memory Manager...\n");
    vmm_init();

    // Identity-map [4MB, 128MB) with the static boot tables (see their
    // declaration above): vmm_init() only maps the low 4MB, but
    //  - the PMM hands out frames up to 16MB (page tables, task stacks,
    //    ELF segment copies are written through physical addresses), and
    //  - the slab writes its free lists INTO buddy pages [16MB, top) and
    //    kmalloc callers dereference them.
    // Kernel-only mappings; PDEs 32+ (user VAs, framebuffer MMIO) are
    // untouched and still created on demand by vmm_map_page(). In the
    // buddy region only MANAGED pages are mapped, so bootinfo holes
    // (ACPI, framebuffer, reserved RAM) never get a stray writable
    // mapping; buddy blocks are always contiguous runs of managed pages,
    // so this covers every address the buddy can ever return. This must
    // run BEFORE fbcon_init(), whose page tables come from the PMM and
    // may themselves live above 4MB.
    {
        uint32_t *dir = (uint32_t *)vmm_get_current_directory();
        for (uint32_t t = 0; t < BOOT_IDENTITY_TABLES; t++) {
            uint32_t chunk_base = (t + 1u) * 0x400000u; /* PDE t+1 */
            for (uint32_t i = 0; i < 1024u; i++) {
                uint32_t addr = chunk_base + i * PAGE_SIZE;
                int mapped = (addr < BUDDY_REGION_START)
                                 ? 1
                                 : buddy_addr_is_managed(addr);
                boot_identity_tables[t][i] =
                    mapped ? (addr | PAGE_PRESENT | PAGE_WRITE) : 0;
            }
            dir[t + 1] = ((uint32_t)(uintptr_t)boot_identity_tables[t]) |
                         PAGE_PRESENT | PAGE_WRITE;
        }
    }

    // Framebuffer splash (M1): needs bootinfo (fb description) AND paging
    // (fbcon identity-maps the fb MMIO range, which lies above the
    // identity-mapped low 4MB), so this is the earliest safe point.
    // No-op on legacy boot paths without a tocinboot framebuffer.
    fbcon_init();

    // Slab caches on top of the buddy: kmalloc/kfree go live here. The
    // existing call sites (net stack, USB, tmpfs/procfs/devfs, fs cache)
    // already call kmalloc — until now it always returned NULL because
    // slab_init() never ran on any boot path.
    serial_printf("[INIT] Slab init...\n");
    kernel_print("[*] Initializing Slab Allocator...\n");
    slab_init();
    serial_printf("[SLAB] caches ready (kmalloc live)\n");

    // Initialize task scheduler
    serial_printf("[INIT] Scheduler init...\n");
    kernel_print("[*] Initializing Task Scheduler...\n");
    scheduler_init();
    
    // Initialize MDF driver framework
    serial_printf("[INIT] MDF init...\n");
    kernel_print("[*] Initializing MDF Driver Framework...\n");
    mdf_init();
    
    // Initialize GDT with user mode segments and TSS
    serial_printf("[INIT] GDT init...\n");
    kernel_print("[*] Initializing GDT with user mode support...\n");
    gdt_init();
    serial_printf("[INIT] GDT done\n");
    
    // Initialize interrupt handling
    serial_printf("[INIT] IDT init...\n");
    kernel_print("[*] Initializing IDT...\n");
    idt_init();
    serial_printf("[INIT] IDT done\n");
    
    serial_printf("[INIT] ISR init...\n");
    kernel_print("[*] Initializing ISR handlers...\n");
    isr_init();
    serial_printf("[INIT] ISR done\n");
    
    // Initialize timer (100 Hz)
    serial_printf("[INIT] Timer init...\n");
    kernel_print("[*] Initializing Timer (100 Hz)...\n");
    timer_init(100);
    serial_printf("[INIT] Timer done\n");
    
    // Initialize keyboard
    serial_printf("[INIT] Keyboard init...\n");
    kernel_print("[*] Initializing Keyboard...\n");
    keyboard_init();
    serial_printf("[INIT] Keyboard done\n");
    
    // Initialize serial port
    kernel_print("[*] Initializing Serial Port (COM1)...\n");
    if (serial_init(COM1) == 0) {
        serial_write(COM1, "TocinOS serial port initialized\n");
    }
    
    // Initialize system calls
    kernel_print("[*] Initializing System Call Interface...\n");
    syscall_init();
    
    // Initialize user mode support
    kernel_print("[*] Initializing User Mode Support...\n");
    if (usermode_init() == 0) {
        kernel_print("    User mode support enabled\n");
    }
    
    // Initialize process management
    kernel_print("[*] Initializing Process Management...\n");
    process_init();
    
    // Initialize VESA graphics
    kernel_print("[*] Initializing VESA Graphics...\n");
    if (vesa_init() == 0) {
        kernel_print("    VESA graphics initialized\n");
    } else {
        kernel_print("    VESA not available\n");
    }
    
    // Initialize IDE disk driver
    kernel_print("[*] Initializing IDE Disk Driver...\n");
    if (ide_init() == 0) {
        kernel_print("    IDE driver initialized\n");
        
        // Test: Read MBR and check signature
        serial_printf("[IDE TEST] Reading MBR from drive 0...\n");
        uint8_t mbr[512];
        int result = ide_read_sector(0, 0, mbr);
        if (result > 0) {
            serial_printf("[IDE TEST] MBR read successful\n");
            serial_printf("[IDE TEST] MBR signature: 0x%x%x\n", mbr[511], mbr[510]);
            if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
                serial_printf("[IDE TEST] Valid boot signature detected!\n");
            }
        } else {
            serial_printf("[IDE TEST] MBR read failed: %d\n", result);
        }
    } else {
        kernel_print("    No IDE drives detected\n");
    }
    
    // Initialize FAT filesystem
    kernel_print("[*] Initializing FAT Filesystem...\n");
    if (fat_init(0) == 0) {
        kernel_print("    FAT filesystem mounted\n");
        
        // Get FAT info for logging
        fat_info_t *info = fat_get_info();
        if (info) {
            serial_printf("[FAT] Type: FAT%d\n", info->type);
            serial_printf("[FAT] Bytes per sector: %u\n", info->bytes_per_sector);
            serial_printf("[FAT] Sectors per cluster: %u\n", info->sectors_per_cluster);
            serial_printf("[FAT] Total clusters: %u\n", info->cluster_count);
        }
        
        // Test: List root directory
        serial_printf("[FAT TEST] Listing root directory...\n");
        fat_dir_entry_t entries[16];
        int num_entries = fat_list_dir("/", entries, 16);
        serial_printf("[FAT TEST] Found %d entries\n", num_entries);
        for (int i = 0; i < num_entries && i < 16; i++) {
            char name[12];
            for (int j = 0; j < 11; j++) {
                name[j] = entries[i].name[j];
            }
            name[11] = 0;
            serial_printf("[FAT TEST]   %s  size=%u\n", name, entries[i].file_size);
        }
    } else {
        kernel_print("    FAT filesystem not found\n");
        serial_printf("[FAT] Failed to mount FAT filesystem\n");
    }
    
    // Initialize VFS
    kernel_print("[*] Initializing VFS...\n");
    vfs_init();
    kernel_print("    VFS initialized\n");
    serial_printf("[INIT] VFS initialized\n");
    
    // Initialize ext4 VFS support
    kernel_print("[*] Initializing ext4 filesystem support...\n");
    ext4_vfs_init();
    kernel_print("    ext4 support registered\n");
    serial_printf("[INIT] ext4 VFS support initialized\n");
    
    // Initialize virtual filesystems
    kernel_print("[*] Initializing procfs (/proc)...\n");
    if (procfs_init() == 0) {
        kernel_print("    procfs initialized\n");
        serial_printf("[INIT] procfs initialized\n");
    }
    
    kernel_print("[*] Initializing devfs (/dev)...\n");
    if (devfs_init() == 0) {
        kernel_print("    devfs initialized\n");
        serial_printf("[INIT] devfs initialized\n");
    }
    
    kernel_print("[*] Initializing tmpfs...\n");
    if (tmpfs_init() == 0) {
        kernel_print("    tmpfs initialized\n");
        serial_printf("[INIT] tmpfs initialized\n");
    }
    
    // Initialize USB subsystem
    kernel_print("[*] Initializing USB subsystem...\n");
    if (usb_init() == 0) {
        kernel_print("    USB core initialized\n");
        serial_printf("[INIT] USB subsystem initialized\n");
        
        // USB HID and Mass Storage drivers will be registered
        extern int usb_hid_init(void);
        extern int usb_msc_init(void);
        usb_hid_init();
        usb_msc_init();
        
        // Enumerate USB devices
        usb_enumerate();
        kernel_print("    USB devices enumerated\n");
    }
    
    // Initialize ELF loader
    kernel_print("[*] Initializing ELF Loader...\n");
    elf_init();
    kernel_print("    ELF loader initialized\n");
    serial_printf("[INIT] ELF loader initialized\n");
    
    // Test all user programs using spawn mechanism
    serial_printf("\n=== MULTI-PROGRAM TEST ===\n");
    
    // Use sys_spawn to run programs (tests spawn/exit return mechanism)
    extern int sys_spawn(uint32_t path, uint32_t argv, uint32_t envp);
    
    // Quick test of echo, hello, ls
    const char *test_programs[] = {"/ECHO.ELF", "/HELLO.ELF", "/LS.ELF", "/DYNHELLO.ELF", (const char*)0};
    
    for (int i = 0; test_programs[i] != (const char*)0; i++) {
        serial_printf("\n--- Running %s ---\n", test_programs[i]);
        int exit_code = sys_spawn((uint32_t)test_programs[i], 0, 0);
        serial_printf("[TEST] %s returned with exit code: %d\n", test_programs[i], exit_code);
    }
    
    serial_printf("\n=== STARTING SHELL ===\n");
    sys_spawn((uint32_t)"/SHELL.ELF", 0, 0);
    serial_printf("\n=== SHELL EXITED ===\n");
    
    // Initialize network driver
    kernel_print("[*] Initializing Network Driver...\n");
    if (net_init() == 0) {
        kernel_print("    Network driver initialized\n");
    } else {
        kernel_print("    No network card detected\n");
    }
    
    kernel_print("\n[OK] Kernel initialization complete!\n");
    kernel_print("[*] System ready.\n");
    
    // Start multitasking
    kernel_print("[*] Starting scheduler...\n");
    scheduler_start();
    
    // Initialize and run shell
    shell_init();
    shell_run();
    
    // Infinite loop (should never reach here)
    while (1) {
        __asm__ volatile("hlt");
    }
}
