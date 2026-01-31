/**
 * TocinOS ELF Executable Loader Implementation
 * 
 * Loads and executes ELF binaries in user mode
 */

#include "../include/kernel/elf.h"
#include "../include/kernel/memory.h"
#include "../include/kernel/usermode.h"

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
    
    const elf32_ehdr_t *header = (const elf32_ehdr_t *)data;
    
    if (context->is_64bit) {
        // 64-bit ELF
        const elf64_ehdr_t *header64 = (const elf64_ehdr_t *)data;
        const elf64_phdr_t *phdr = (const elf64_phdr_t *)((const uint8_t *)data + header64->e_phoff);
        
        for (int i = 0; i < header64->e_phnum; i++) {
            if (phdr[i].p_type == PT_LOAD) {
                // Allocate memory for segment
                void *segment_mem = pmm_alloc_page(); // TODO: Allocate correct size
                if (!segment_mem) {
                    return -1;
                }
                
                // Copy segment data
                const uint8_t *segment_data = (const uint8_t *)data + phdr[i].p_offset;
                uint8_t *dest = (uint8_t *)segment_mem;
                
                for (uint64_t j = 0; j < phdr[i].p_filesz; j++) {
                    dest[j] = segment_data[j];
                }
                
                // Zero out BSS section
                for (uint64_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) {
                    dest[j] = 0;
                }
                
                // TODO: Map segment to virtual address p_vaddr
            }
        }
    } else {
        // 32-bit ELF
        const elf32_phdr_t *phdr = (const elf32_phdr_t *)((const uint8_t *)data + header->e_phoff);
        
        for (int i = 0; i < header->e_phnum; i++) {
            if (phdr[i].p_type == PT_LOAD) {
                // Allocate memory for segment
                void *segment_mem = pmm_alloc_page(); // TODO: Allocate correct size
                if (!segment_mem) {
                    return -1;
                }
                
                // Copy segment data
                const uint8_t *segment_data = (const uint8_t *)data + phdr[i].p_offset;
                uint8_t *dest = (uint8_t *)segment_mem;
                
                for (uint32_t j = 0; j < phdr[i].p_filesz; j++) {
                    dest[j] = segment_data[j];
                }
                
                // Zero out BSS section
                for (uint32_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) {
                    dest[j] = 0;
                }
                
                // TODO: Map segment to virtual address p_vaddr
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
    
    // Load segments
    if (elf_load_segments(data, context) != 0) {
        return -1;
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
    
    // Create user mode process
    uint32_t pid;
    void (*entry_point)(void) = (void (*)(void))context->entry_point;
    
    if (usermode_create_process(entry_point, &pid) != 0) {
        return -1;
    }
    
    // Switch to user mode
    usermode_switch_to_user(entry_point);
    
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
    extern void serial_printf(const char *fmt, ...);
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
