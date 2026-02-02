/**
 * TocinOS ELF Executable Loader Implementation
 * 
 * Loads and executes ELF binaries in user mode
 */

#include "../include/kernel/elf.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/usermode.h"
#include "../include/kernel/serial.h"

static int elf_initialized = 0;

/**
 * Initialize ELF loader
 */
int elf_init(void) {
    if (elf_initialized) {
        return 0;
    }
    
    elf_initialized = 1;
    return 0;
}

/**
 * Validate ELF file
 */
int elf_validate(const void *data, uint32_t size) {
    if (!data || size < sizeof(elf32_ehdr_t)) {
        return -1;
    }
    
    const elf32_ehdr_t *header = (const elf32_ehdr_t *)data;
    
    // Check magic number
    if (header->e_ident[EI_MAG0] != 0x7F ||
        header->e_ident[EI_MAG1] != 'E' ||
        header->e_ident[EI_MAG2] != 'L' ||
        header->e_ident[EI_MAG3] != 'F') {
        return -1;
    }
    
    // Check class (32-bit or 64-bit)
    if (header->e_ident[EI_CLASS] != ELFCLASS32 &&
        header->e_ident[EI_CLASS] != ELFCLASS64) {
        return -1;
    }
    
    // Check data encoding (little endian)
    if (header->e_ident[EI_DATA] != ELFDATA2LSB) {
        return -1;
    }
    
    // Check version
    if (header->e_ident[EI_VERSION] != 1) {
        return -1;
    }
    
    // Check file type (must be executable)
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        return -1;
    }
    
    // Check machine type
    if (header->e_machine != EM_386 && header->e_machine != EM_X86_64) {
        return -1;
    }
    
    return 0;
}

/**
 * Parse ELF header
 */
int elf_parse_header(const void *data, elf_context_t *context) {
    if (!data || !context) {
        return -1;
    }
    
    const elf32_ehdr_t *header = (const elf32_ehdr_t *)data;
    
    // Determine if 32-bit or 64-bit
    context->is_64bit = (header->e_ident[EI_CLASS] == ELFCLASS64);
    
    if (context->is_64bit) {
        const elf64_ehdr_t *header64 = (const elf64_ehdr_t *)data;
        context->entry_point = (uint32_t)header64->e_entry;
    } else {
        context->entry_point = header->e_entry;
    }
    
    return 0;
}

/**
 * Get entry point from ELF
 */
uint32_t elf_get_entry_point(const void *data) {
    if (!data) {
        return 0;
    }
    
    const elf32_ehdr_t *header = (const elf32_ehdr_t *)data;
    
    if (header->e_ident[EI_CLASS] == ELFCLASS64) {
        const elf64_ehdr_t *header64 = (const elf64_ehdr_t *)data;
        return (uint32_t)header64->e_entry;
    } else {
        return header->e_entry;
    }
}

/**
 * Load ELF segments into memory
 */
int elf_load_segments(const void *data, elf_context_t *context) {
    if (!data || !context) {
        return -1;
    }
    
    extern void vmm_map_page(unsigned int vaddr, unsigned int paddr, unsigned int flags);
    
    const elf32_ehdr_t *header = (const elf32_ehdr_t *)data;
    
    if (context->is_64bit) {
        return -1;  // 64-bit not supported
    }
    
    // 32-bit ELF
    const elf32_phdr_t *phdr = (const elf32_phdr_t *)((const uint8_t *)data + header->e_phoff);
    
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD && phdr[i].p_memsz > 0) {
            uint32_t vaddr = phdr[i].p_vaddr;
            uint32_t memsz = phdr[i].p_memsz;
            uint32_t filesz = phdr[i].p_filesz;
            uint32_t offset = phdr[i].p_offset;
            
            // Calculate page range
            uint32_t page_start = vaddr & ~0xFFF;
            uint32_t page_end = (vaddr + memsz + 0xFFF) & ~0xFFF;
            
            // Map each page
            for (uint32_t pv = page_start; pv < page_end; pv += 4096) {
                uint32_t paddr = (uint32_t)pmm_alloc_page();
                if (!paddr) return -1;
                
                // Zero the page via physical address
                uint8_t *p = (uint8_t *)paddr;
                for (int j = 0; j < 4096; j++) p[j] = 0;
                
                // Map virtual to physical
                vmm_map_page(pv, paddr, PAGE_WRITE | PAGE_USER);
                __asm__ volatile("invlpg (%0)" : : "r"(pv) : "memory");
            }
            
            // Copy file data to virtual address (now mapped)
            if (filesz > 0) {
                const uint8_t *src = (const uint8_t *)data + offset;
                uint8_t *dest = (uint8_t *)vaddr;
                for (uint32_t j = 0; j < filesz; j++) {
                    dest[j] = src[j];
                }
            }
        }
    }
    
    return 0;
}

/**
 * Load ELF binary
 */
int elf_load(const void *data, uint32_t size, elf_context_t *context) {
    if (!elf_initialized || !data || !context) {
        return -1;
    }
    
    // Validate ELF
    if (elf_validate(data, size) != 0) {
        return -1;
    }
    
    // Parse header
    if (elf_parse_header(data, context) != 0) {
        return -1;
    }
    
    // Store ELF data
    context->elf_data = (void *)data;
    context->elf_size = size;
    context->load_base = 0x400000; // Default load base
    
    // Store program header info
    const elf32_ehdr_t *header = (const elf32_ehdr_t *)data;
    context->phdr = (void *)((uint8_t *)data + header->e_phoff);
    context->phnum = header->e_phnum;
    context->phent = header->e_phentsize;
    
    // Check for interpreter (dynamic linker)
    if (elf_find_interp(data, context) != 0) {
        serial_printf("[ELF] Warning: Failed to check for interpreter\n");
    }
    
    // Load segments
    if (elf_load_segments(data, context) != 0) {
        return -1;
    }
    
    // Parse dynamic section if present
    if (elf_parse_dynamic(data, context) != 0) {
        serial_printf("[ELF] Warning: Failed to parse dynamic section\n");
    }
    
    // Process relocations for dynamic binaries
    if (context->is_dynamic) {
        if (elf_process_relocations(context) != 0) {
            serial_printf("[ELF] Warning: Some relocations may have failed\n");
        }
        
        // Call initialization functions
        if (elf_call_init(context) != 0) {
            serial_printf("[ELF] Warning: Init functions may have failed\n");
        }
    }
    
    return 0;
}

/**
 * Execute ELF binary
 */
int elf_execute(elf_context_t *context) {
    if (!elf_initialized || !context) {
        return -1;
    }
    
    extern void vmm_map_page(unsigned int vaddr, unsigned int paddr, unsigned int flags);
    extern void tss_set_kernel_stack(uint32_t stack);
    extern void usermode_switch_to_user_with_stack(void *entry, uint32_t user_esp);
    #define PAGE_WRITE 0x02
    #define PAGE_USER  0x04
    
    // Allocate a kernel stack for syscalls
    uint32_t kernel_stack_paddr = (uint32_t)pmm_alloc_page();
    if (!kernel_stack_paddr) return -1;
    
    // Zero the kernel stack
    uint8_t *ks = (uint8_t *)kernel_stack_paddr;
    for (int j = 0; j < 4096; j++) ks[j] = 0;
    
    // Set TSS kernel stack (top of page since stack grows down)
    uint32_t kernel_stack_top = kernel_stack_paddr + 4096 - 16;
    tss_set_kernel_stack(kernel_stack_top);
    
    // Map user stack pages
    uint32_t stack_top = 0xBFFFF000;
    for (int i = 0; i < 4; i++) {
        uint32_t stack_vaddr = stack_top - ((i + 1) * 4096);
        uint32_t stack_paddr = (uint32_t)pmm_alloc_page();
        if (!stack_paddr) return -1;
        
        uint8_t *p = (uint8_t *)stack_paddr;
        for (int j = 0; j < 4096; j++) p[j] = 0;
        
        vmm_map_page(stack_vaddr, stack_paddr, PAGE_WRITE | PAGE_USER);
        __asm__ volatile("invlpg (%0)" : : "r"(stack_vaddr) : "memory");
    }
    
    // Determine actual entry point (may be interpreter)
    uint32_t actual_entry = context->entry_point;
    uint32_t interp_base = 0;
    
    // Check if we need to load an interpreter (dynamic linker)
    if (context->dyn.interp_path[0] != '\0') {
        serial_printf("[ELF] Found interpreter: %s\n", context->dyn.interp_path);
        
        // Load the interpreter (ld.so)
        typedef struct { uint32_t cluster; uint32_t size; uint32_t position; uint32_t current_cluster; } fat_file_t;
        extern fat_file_t *fat_open(const char *path);
        extern int fat_read(fat_file_t *file, void *buffer, uint32_t size);
        extern void fat_close(fat_file_t *file);
        
        fat_file_t *interp_file = fat_open(context->dyn.interp_path);
        if (interp_file) {
            uint32_t interp_size = interp_file->size;
            if (interp_size > 0 && interp_size < 64*1024) {
                static uint8_t interp_buf[64*1024];
                int bytes_read = fat_read(interp_file, interp_buf, interp_size);
                fat_close(interp_file);
                
                if (bytes_read > 0 && elf_validate(interp_buf, bytes_read)) {
                    elf32_ehdr_t *interp_hdr = (elf32_ehdr_t *)interp_buf;
                    elf32_phdr_t *interp_phdr = (elf32_phdr_t *)(interp_buf + interp_hdr->e_phoff);
                    
                    // For ET_EXEC interpreters, load at their specified addresses
                    // For ET_DYN interpreters, use a fixed base
                    uint32_t interp_load_base = 0;
                    if (interp_hdr->e_type == ET_DYN) {
                        interp_load_base = 0x40000000;
                    }
                    interp_base = interp_load_base;
                    
                    // Map interpreter segments
                    for (int i = 0; i < interp_hdr->e_phnum; i++) {
                        if (interp_phdr[i].p_type == PT_LOAD) {
                            uint32_t vaddr = interp_load_base + interp_phdr[i].p_vaddr;
                            uint32_t memsz = interp_phdr[i].p_memsz;
                            uint32_t filesz = interp_phdr[i].p_filesz;
                            
                            for (uint32_t off = 0; off < memsz; off += 4096) {
                                uint32_t page_vaddr = (vaddr + off) & ~0xFFF;
                                uint32_t paddr = (uint32_t)pmm_alloc_page();
                                if (paddr) {
                                    uint8_t *p = (uint8_t *)paddr;
                                    for (int j = 0; j < 4096; j++) p[j] = 0;
                                    vmm_map_page(page_vaddr, paddr, PAGE_WRITE | PAGE_USER);
                                    __asm__ volatile("invlpg (%0)" : : "r"(page_vaddr) : "memory");
                                }
                            }
                            
                            // Copy segment contents
                            uint8_t *src = interp_buf + interp_phdr[i].p_offset;
                            uint8_t *dst = (uint8_t *)vaddr;
                            for (uint32_t j = 0; j < filesz; j++) {
                                dst[j] = src[j];
                            }
                        }
                    }
                    
                    // Use interpreter's entry point
                    actual_entry = interp_load_base + interp_hdr->e_entry;
                }
            }
        }
    }
    
    // Set up user stack with argc, argv, envp, auxv
    // First copy program headers to user-accessible stack
    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)context->elf_data;
    uint32_t phdr_size = ehdr->e_phnum * ehdr->e_phentsize;
    uint32_t phdr_stack_addr = stack_top - phdr_size - 16;
    phdr_stack_addr &= ~0xF;
    
    uint8_t *phdr_src = (uint8_t *)context->elf_data + ehdr->e_phoff;
    uint8_t *phdr_dst = (uint8_t *)phdr_stack_addr;
    for (uint32_t i = 0; i < phdr_size; i++) {
        phdr_dst[i] = phdr_src[i];
    }
    
    // Build stack: argc, argv[], NULL, envp[], NULL, auxv[]
    uint32_t stack_base = phdr_stack_addr - 256;
    uint32_t *stack = (uint32_t *)stack_base;
    int idx = 0;
    
    stack[idx++] = 1;  // argc
    stack[idx++] = 0;  // argv[0] = NULL
    stack[idx++] = 0;  // NULL terminator
    stack[idx++] = 0;  // envp NULL terminator
    
    // Auxiliary vector
    stack[idx++] = AT_PHDR;
    stack[idx++] = phdr_stack_addr;
    stack[idx++] = AT_PHENT;
    stack[idx++] = context->phent;
    stack[idx++] = AT_PHNUM;
    stack[idx++] = context->phnum;
    stack[idx++] = AT_PAGESZ;
    stack[idx++] = 4096;
    stack[idx++] = AT_BASE;
    stack[idx++] = interp_base;
    stack[idx++] = AT_ENTRY;
    stack[idx++] = context->entry_point;
    stack[idx++] = AT_NULL;
    stack[idx++] = 0;
    
    uint32_t user_esp = stack_base;
    
    // Create user mode process
    uint32_t pid;
    void (*entry_fn)(void) = (void (*)(void))actual_entry;
    
    if (usermode_create_process(entry_fn, &pid) != 0) {
        return -1;
    }
    
    // Switch to user mode with prepared stack
    usermode_switch_to_user_with_stack(entry_fn, user_esp);
    
    return 0;
}

/**
 * Unload ELF binary
 */
int elf_unload(elf_context_t *context) {
    if (!elf_initialized || !context) {
        return -1;
    }
    
    // TODO: Free allocated memory
    // TODO: Unmap segments
    
    context->elf_data = 0;
    context->elf_size = 0;
    context->entry_point = 0;
    context->load_base = 0;
    
    return 0;
}

/**
 * Get ELF type string
 */
const char *elf_get_type_string(uint16_t type) {
    switch (type) {
        case ET_NONE: return "NONE";
        case ET_REL:  return "REL";
        case ET_EXEC: return "EXEC";
        case ET_DYN:  return "DYN";
        case ET_CORE: return "CORE";
        default:      return "UNKNOWN";
    }
}

/**
 * Get ELF machine string
 */
const char *elf_get_machine_string(uint16_t machine) {
    switch (machine) {
        case EM_NONE:    return "NONE";
        case EM_386:     return "Intel 80386";
        case EM_X86_64:  return "AMD x86-64";
        default:         return "UNKNOWN";
    }
}

/**
 * Load ELF binary from filesystem path
 */
int elf_load_file(const char *path, elf_context_t *context) {
    extern uint32_t pmm_alloc_page(void);
    extern int vfs_open(const char *path, int flags);
    extern int vfs_read(int fd, void *buf, uint32_t size);
    extern int vfs_close(int fd);
    
    if (!elf_initialized || !path || !context) {
        return -1;
    }
    
    // Open file
    int fd = vfs_open(path, 0);
    if (fd < 0) {
        return -1;
    }
    
    // Allocate buffer for ELF file (64KB max)
    uint8_t *buffer = (uint8_t *)pmm_alloc_page();
    if (!buffer) {
        vfs_close(fd);
        return -1;
    }
    
    // Allocate more pages for larger files (up to 64KB)
    for (int i = 1; i < 16; i++) {
        pmm_alloc_page();  // Allocate contiguous pages
    }
    
    // Read file contents
    int bytes_read = vfs_read(fd, buffer, 64 * 1024);
    vfs_close(fd);
    
    if (bytes_read <= 0) {
        return -1;
    }
    
    // Load ELF from buffer
    return elf_load(buffer, bytes_read, context);
}

/**
 * Find interpreter (PT_INTERP) in ELF file
 */
int elf_find_interp(const void *data, elf_context_t *context) {
    if (!data || !context) {
        return -1;
    }
    
    const elf32_ehdr_t *header = (const elf32_ehdr_t *)data;
    const elf32_phdr_t *phdr = (const elf32_phdr_t *)((const uint8_t *)data + header->e_phoff);
    
    context->dyn.needs_interp = 0;
    context->dyn.interp_path[0] = '\0';
    
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type == PT_INTERP) {
            // Found interpreter segment
            const char *interp = (const char *)data + phdr[i].p_offset;
            
            // Copy interpreter path
            int j = 0;
            while (interp[j] && j < 255) {
                context->dyn.interp_path[j] = interp[j];
                j++;
            }
            context->dyn.interp_path[j] = '\0';
            context->dyn.needs_interp = 1;
            
            serial_printf("[ELF] Found interpreter: %s\n", context->dyn.interp_path);
            return 0;
        }
    }
    
    return 0;  // No interpreter needed (statically linked)
}

/**
 * Parse dynamic section
 */
int elf_parse_dynamic(const void *data, elf_context_t *context) {
    if (!data || !context) {
        return -1;
    }
    
    const elf32_ehdr_t *header = (const elf32_ehdr_t *)data;
    const elf32_phdr_t *phdr = (const elf32_phdr_t *)((const uint8_t *)data + header->e_phoff);
    
    // Find PT_DYNAMIC segment
    elf32_dyn_t *dynamic = 0;
    uint32_t dynamic_vaddr = 0;
    
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dynamic = (elf32_dyn_t *)((uint8_t *)data + phdr[i].p_offset);
            dynamic_vaddr = phdr[i].p_vaddr;
            break;
        }
    }
    
    if (!dynamic) {
        // Not dynamically linked
        context->is_dynamic = 0;
        return 0;
    }
    
    context->is_dynamic = 1;
    context->dyn.dynamic = dynamic;
    
    serial_printf("[ELF] Parsing dynamic section at 0x%08X\n", dynamic_vaddr);
    
    // Parse dynamic entries
    uint32_t strtab_offset = 0;
    uint32_t symtab_offset = 0;
    uint32_t rel_offset = 0;
    uint32_t rel_size = 0;
    uint32_t plt_rel_offset = 0;
    uint32_t plt_rel_size = 0;
    
    for (elf32_dyn_t *dyn = dynamic; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
            case DT_STRTAB:
                strtab_offset = dyn->d_un.d_ptr;
                break;
            case DT_SYMTAB:
                symtab_offset = dyn->d_un.d_ptr;
                break;
            case DT_REL:
                rel_offset = dyn->d_un.d_ptr;
                break;
            case DT_RELSZ:
                rel_size = dyn->d_un.d_val;
                break;
            case DT_JMPREL:
                plt_rel_offset = dyn->d_un.d_ptr;
                break;
            case DT_PLTRELSZ:
                plt_rel_size = dyn->d_un.d_val;
                break;
            case DT_PLTGOT:
                context->dyn.got = (uint32_t *)(context->load_base + dyn->d_un.d_ptr);
                break;
            case DT_INIT:
                context->dyn.init_func = (void (*)(void))(context->load_base + dyn->d_un.d_ptr);
                serial_printf("[ELF] Init function at 0x%08X\n", dyn->d_un.d_ptr);
                break;
            case DT_FINI:
                context->dyn.fini_func = (void (*)(void))(context->load_base + dyn->d_un.d_ptr);
                break;
            case DT_INIT_ARRAY:
                context->dyn.init_array = (void (**)(void))(context->load_base + dyn->d_un.d_ptr);
                break;
            case DT_INIT_ARRAYSZ:
                context->dyn.init_array_sz = dyn->d_un.d_val;
                break;
            case DT_FINI_ARRAY:
                context->dyn.fini_array = (void (**)(void))(context->load_base + dyn->d_un.d_ptr);
                break;
            case DT_FINI_ARRAYSZ:
                context->dyn.fini_array_sz = dyn->d_un.d_val;
                break;
            case DT_NEEDED:
                // Log needed library
                serial_printf("[ELF] Needs library: offset=%u\n", dyn->d_un.d_val);
                break;
            case DT_SONAME:
                context->dyn.soname_offset = dyn->d_un.d_val;
                break;
            case DT_HASH:
                context->dyn.hash = (uint32_t *)(context->load_base + dyn->d_un.d_ptr);
                break;
        }
    }
    
    // Convert offsets to pointers (relative to load base for runtime)
    // For file parsing, use file offsets
    // Find the offset within the file for each address
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            if (strtab_offset >= phdr[i].p_vaddr && 
                strtab_offset < phdr[i].p_vaddr + phdr[i].p_memsz) {
                uint32_t file_off = strtab_offset - phdr[i].p_vaddr + phdr[i].p_offset;
                context->dyn.strtab = (char *)((uint8_t *)data + file_off);
            }
            if (symtab_offset >= phdr[i].p_vaddr && 
                symtab_offset < phdr[i].p_vaddr + phdr[i].p_memsz) {
                uint32_t file_off = symtab_offset - phdr[i].p_vaddr + phdr[i].p_offset;
                context->dyn.symtab = (elf32_sym_t *)((uint8_t *)data + file_off);
            }
            if (rel_offset && rel_offset >= phdr[i].p_vaddr && 
                rel_offset < phdr[i].p_vaddr + phdr[i].p_memsz) {
                uint32_t file_off = rel_offset - phdr[i].p_vaddr + phdr[i].p_offset;
                context->dyn.rel = (elf32_rel_t *)((uint8_t *)data + file_off);
                context->dyn.rel_count = rel_size / sizeof(elf32_rel_t);
            }
            if (plt_rel_offset && plt_rel_offset >= phdr[i].p_vaddr && 
                plt_rel_offset < phdr[i].p_vaddr + phdr[i].p_memsz) {
                uint32_t file_off = plt_rel_offset - phdr[i].p_vaddr + phdr[i].p_offset;
                context->dyn.plt_rel = (elf32_rel_t *)((uint8_t *)data + file_off);
                context->dyn.plt_rel_count = plt_rel_size / sizeof(elf32_rel_t);
            }
        }
    }
    
    serial_printf("[ELF] Dynamic parsing complete: strtab=%p, symtab=%p\n",
                  context->dyn.strtab, context->dyn.symtab);
    serial_printf("[ELF] REL: %u entries, PLT REL: %u entries\n",
                  context->dyn.rel_count, context->dyn.plt_rel_count);
    
    return 0;
}

/**
 * Look up a symbol by name
 */
void *elf_lookup_symbol(elf_context_t *context, const char *name) {
    if (!context || !name || !context->dyn.symtab || !context->dyn.strtab) {
        return 0;
    }
    
    // If we have a hash table, use it for O(1) lookup
    if (context->dyn.hash) {
        uint32_t nbucket = context->dyn.hash[0];
        uint32_t nchain = context->dyn.hash[1];
        uint32_t *bucket = &context->dyn.hash[2];
        uint32_t *chain = &context->dyn.hash[2 + nbucket];
        
        // ELF hash function
        uint32_t hash = 0;
        const uint8_t *p = (const uint8_t *)name;
        while (*p) {
            hash = (hash << 4) + *p++;
            uint32_t g = hash & 0xf0000000;
            if (g) {
                hash ^= g >> 24;
            }
            hash &= ~g;
        }
        
        // Look up in hash table
        uint32_t idx = bucket[hash % nbucket];
        while (idx != 0 && idx < nchain) {
            elf32_sym_t *sym = &context->dyn.symtab[idx];
            const char *sym_name = &context->dyn.strtab[sym->st_name];
            
            // Compare names
            int match = 1;
            const char *a = name;
            const char *b = sym_name;
            while (*a && *b) {
                if (*a++ != *b++) {
                    match = 0;
                    break;
                }
            }
            if (match && *a == *b) {
                // Found it
                if (sym->st_shndx != 0) {  // Not undefined
                    return (void *)(context->load_base + sym->st_value);
                }
            }
            
            idx = chain[idx];
        }
    }
    
    return 0;  // Symbol not found
}

/**
 * Apply a single relocation
 */
int elf_apply_relocation(elf_context_t *context, elf32_rel_t *rel, uint32_t base) {
    uint32_t type = ELF32_R_TYPE(rel->r_info);
    uint32_t sym_idx = ELF32_R_SYM(rel->r_info);
    
    uint32_t *target = (uint32_t *)(base + rel->r_offset);
    uint32_t sym_val = 0;
    
    // Get symbol value if needed
    if (sym_idx != 0 && context->dyn.symtab) {
        elf32_sym_t *sym = &context->dyn.symtab[sym_idx];
        if (sym->st_shndx != 0) {
            sym_val = base + sym->st_value;
        } else {
            // External symbol - need to resolve
            const char *sym_name = &context->dyn.strtab[sym->st_name];
            serial_printf("[ELF] External symbol: %s (unresolved)\n", sym_name);
            // TODO: Look up in loaded libraries
            return -1;
        }
    }
    
    switch (type) {
        case R_386_NONE:
            break;
            
        case R_386_32:
            // Direct 32-bit: S + A
            *target += sym_val;
            break;
            
        case R_386_PC32:
            // PC-relative 32-bit: S + A - P
            *target += sym_val - (uint32_t)target;
            break;
            
        case R_386_GLOB_DAT:
        case R_386_JMP_SLOT:
            // GOT entry / PLT entry: S
            *target = sym_val;
            break;
            
        case R_386_RELATIVE:
            // Relative: B + A (where B is base address)
            *target += base;
            break;
            
        case R_386_COPY:
            // Copy symbol value
            // TODO: Implement for data symbols
            break;
            
        default:
            serial_printf("[ELF] Unknown relocation type: %u\n", type);
            return -1;
    }
    
    return 0;
}

/**
 * Process all relocations
 */
int elf_process_relocations(elf_context_t *context) {
    if (!context) {
        return -1;
    }
    
    serial_printf("[ELF] Processing relocations...\n");
    
    uint32_t base = context->load_base;
    
    // Process REL relocations
    if (context->dyn.rel && context->dyn.rel_count > 0) {
        serial_printf("[ELF] Processing %u REL relocations\n", context->dyn.rel_count);
        for (uint32_t i = 0; i < context->dyn.rel_count; i++) {
            if (elf_apply_relocation(context, &context->dyn.rel[i], base) < 0) {
                serial_printf("[ELF] Failed to apply REL relocation %u\n", i);
                // Continue anyway - some failures are OK for weak symbols
            }
        }
    }
    
    // Process PLT relocations
    if (context->dyn.plt_rel && context->dyn.plt_rel_count > 0) {
        serial_printf("[ELF] Processing %u PLT relocations\n", context->dyn.plt_rel_count);
        for (uint32_t i = 0; i < context->dyn.plt_rel_count; i++) {
            if (elf_apply_relocation(context, &context->dyn.plt_rel[i], base) < 0) {
                serial_printf("[ELF] Failed to apply PLT relocation %u\n", i);
            }
        }
    }
    
    serial_printf("[ELF] Relocations complete\n");
    return 0;
}

/**
 * Call initialization functions
 */
int elf_call_init(elf_context_t *context) {
    if (!context) {
        return -1;
    }
    
    // Call init function if present
    if (context->dyn.init_func) {
        serial_printf("[ELF] Calling init function at %p\n", context->dyn.init_func);
        context->dyn.init_func();
    }
    
    // Call init array if present
    if (context->dyn.init_array && context->dyn.init_array_sz > 0) {
        uint32_t count = context->dyn.init_array_sz / sizeof(void *);
        serial_printf("[ELF] Calling %u init array functions\n", count);
        for (uint32_t i = 0; i < count; i++) {
            if (context->dyn.init_array[i]) {
                context->dyn.init_array[i]();
            }
        }
    }
    
    return 0;
}

/**
 * Call finalization functions
 */
void elf_call_fini(elf_context_t *context) {
    if (!context) {
        return;
    }
    
    // Call fini array in reverse order
    if (context->dyn.fini_array && context->dyn.fini_array_sz > 0) {
        uint32_t count = context->dyn.fini_array_sz / sizeof(void *);
        serial_printf("[ELF] Calling %u fini array functions\n", count);
        for (int i = count - 1; i >= 0; i--) {
            if (context->dyn.fini_array[i]) {
                context->dyn.fini_array[i]();
            }
        }
    }
    
    // Call fini function if present
    if (context->dyn.fini_func) {
        serial_printf("[ELF] Calling fini function at %p\n", context->dyn.fini_func);
        context->dyn.fini_func();
    }
}

/**
 * Build auxiliary vector for program startup
 */
int elf_build_auxv(elf_context_t *context, uint32_t *stack, int *auxv_count) {
    if (!context || !stack || !auxv_count) {
        return -1;
    }
    
    int idx = 0;
    
    // AT_PHDR - program headers address
    stack[idx++] = AT_PHDR;
    stack[idx++] = context->load_base + ((elf32_ehdr_t *)context->elf_data)->e_phoff;
    
    // AT_PHENT - program header entry size
    stack[idx++] = AT_PHENT;
    stack[idx++] = context->phent;
    
    // AT_PHNUM - number of program headers
    stack[idx++] = AT_PHNUM;
    stack[idx++] = context->phnum;
    
    // AT_PAGESZ - page size
    stack[idx++] = AT_PAGESZ;
    stack[idx++] = 4096;
    
    // AT_BASE - interpreter base (0 if no interpreter)
    stack[idx++] = AT_BASE;
    stack[idx++] = 0;  // TODO: Set if interpreter is loaded
    
    // AT_FLAGS
    stack[idx++] = AT_FLAGS;
    stack[idx++] = 0;
    
    // AT_ENTRY - program entry point
    stack[idx++] = AT_ENTRY;
    stack[idx++] = context->entry_point;
    
    // AT_UID, AT_EUID, AT_GID, AT_EGID
    stack[idx++] = AT_UID;
    stack[idx++] = 0;  // root for now
    stack[idx++] = AT_EUID;
    stack[idx++] = 0;
    stack[idx++] = AT_GID;
    stack[idx++] = 0;
    stack[idx++] = AT_EGID;
    stack[idx++] = 0;
    
    // AT_NULL - end of auxiliary vector
    stack[idx++] = AT_NULL;
    stack[idx++] = 0;
    
    *auxv_count = idx;
    return 0;
}
