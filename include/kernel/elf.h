/**
 * TocinOS ELF Executable Loader
 * 
 * Loads and executes ELF (Executable and Linkable Format) binaries
 */

#ifndef ELF_H
#define ELF_H

#include "../stdint.h"

// ELF identification indexes
#define EI_MAG0         0   // 0x7F
#define EI_MAG1         1   // 'E'
#define EI_MAG2         2   // 'L'
#define EI_MAG3         3   // 'F'
#define EI_CLASS        4   // File class
#define EI_DATA         5   // Data encoding
#define EI_VERSION      6   // File version
#define EI_OSABI        7   // OS/ABI identification
#define EI_ABIVERSION   8   // ABI version
#define EI_PAD          9   // Start of padding bytes
#define EI_NIDENT       16  // Size of e_ident[]

// ELF classes
#define ELFCLASSNONE    0   // Invalid class
#define ELFCLASS32      1   // 32-bit objects
#define ELFCLASS64      2   // 64-bit objects

// Data encoding
#define ELFDATANONE     0   // Invalid data encoding
#define ELFDATA2LSB     1   // Little endian
#define ELFDATA2MSB     2   // Big endian

// Object file types
#define ET_NONE         0   // No file type
#define ET_REL          1   // Relocatable file
#define ET_EXEC         2   // Executable file
#define ET_DYN          3   // Shared object file
#define ET_CORE         4   // Core file

// Machine types
#define EM_NONE         0   // No machine
#define EM_386          3   // Intel 80386
#define EM_X86_64       62  // AMD x86-64

// Program header types
#define PT_NULL         0   // Unused entry
#define PT_LOAD         1   // Loadable segment
#define PT_DYNAMIC      2   // Dynamic linking information
#define PT_INTERP       3   // Interpreter information
#define PT_NOTE         4   // Auxiliary information
#define PT_SHLIB        5   // Reserved
#define PT_PHDR         6   // Program header table

// Program header flags
#define PF_X            0x1 // Execute
#define PF_W            0x2 // Write
#define PF_R            0x4 // Read

// Section header types
#define SHT_NULL        0   // Unused
#define SHT_PROGBITS    1   // Program data
#define SHT_SYMTAB      2   // Symbol table
#define SHT_STRTAB      3   // String table
#define SHT_RELA        4   // Relocation entries with addends
#define SHT_HASH        5   // Symbol hash table
#define SHT_DYNAMIC     6   // Dynamic linking information
#define SHT_NOTE        7   // Notes
#define SHT_NOBITS      8   // Program space with no data (bss)
#define SHT_REL         9   // Relocation entries, no addends
#define SHT_SHLIB       10  // Reserved
#define SHT_DYNSYM      11  // Dynamic linker symbol table

// ELF32 header
typedef struct {
    uint8_t  e_ident[EI_NIDENT]; // ELF identification
    uint16_t e_type;              // Object file type
    uint16_t e_machine;           // Machine type
    uint32_t e_version;           // Object file version
    uint32_t e_entry;             // Entry point address
    uint32_t e_phoff;             // Program header offset
    uint32_t e_shoff;             // Section header offset
    uint32_t e_flags;             // Processor-specific flags
    uint16_t e_ehsize;            // ELF header size
    uint16_t e_phentsize;         // Program header entry size
    uint16_t e_phnum;             // Program header entry count
    uint16_t e_shentsize;         // Section header entry size
    uint16_t e_shnum;             // Section header entry count
    uint16_t e_shstrndx;          // Section name string table index
} __attribute__((packed)) elf32_ehdr_t;

// ELF32 program header
typedef struct {
    uint32_t p_type;              // Segment type
    uint32_t p_offset;            // Segment file offset
    uint32_t p_vaddr;             // Segment virtual address
    uint32_t p_paddr;             // Segment physical address
    uint32_t p_filesz;            // Segment size in file
    uint32_t p_memsz;             // Segment size in memory
    uint32_t p_flags;             // Segment flags
    uint32_t p_align;             // Segment alignment
} __attribute__((packed)) elf32_phdr_t;

// ELF32 section header
typedef struct {
    uint32_t sh_name;             // Section name (string table index)
    uint32_t sh_type;             // Section type
    uint32_t sh_flags;            // Section flags
    uint32_t sh_addr;             // Section virtual address
    uint32_t sh_offset;           // Section file offset
    uint32_t sh_size;             // Section size in bytes
    uint32_t sh_link;             // Link to another section
    uint32_t sh_info;             // Additional section information
    uint32_t sh_addralign;        // Section alignment
    uint32_t sh_entsize;          // Entry size if section holds table
} __attribute__((packed)) elf32_shdr_t;

// ELF64 header
typedef struct {
    uint8_t  e_ident[EI_NIDENT]; // ELF identification
    uint16_t e_type;              // Object file type
    uint16_t e_machine;           // Machine type
    uint32_t e_version;           // Object file version
    uint64_t e_entry;             // Entry point address
    uint64_t e_phoff;             // Program header offset
    uint64_t e_shoff;             // Section header offset
    uint32_t e_flags;             // Processor-specific flags
    uint16_t e_ehsize;            // ELF header size
    uint16_t e_phentsize;         // Program header entry size
    uint16_t e_phnum;             // Program header entry count
    uint16_t e_shentsize;         // Section header entry size
    uint16_t e_shnum;             // Section header entry count
    uint16_t e_shstrndx;          // Section name string table index
} __attribute__((packed)) elf64_ehdr_t;

// ELF64 program header
typedef struct {
    uint32_t p_type;              // Segment type
    uint32_t p_flags;             // Segment flags
    uint64_t p_offset;            // Segment file offset
    uint64_t p_vaddr;             // Segment virtual address
    uint64_t p_paddr;             // Segment physical address
    uint64_t p_filesz;            // Segment size in file
    uint64_t p_memsz;             // Segment size in memory
    uint64_t p_align;             // Segment alignment
} __attribute__((packed)) elf64_phdr_t;

// ELF loader context
typedef struct {
    void *elf_data;               // ELF file data
    uint32_t elf_size;            // ELF file size
    uint32_t entry_point;         // Entry point address
    uint32_t load_base;           // Load base address
    uint32_t is_64bit;            // 1 if 64-bit ELF
} elf_context_t;

// ELF loader API
int elf_init(void);
int elf_validate(const void *data, uint32_t size);
int elf_load(const void *data, uint32_t size, elf_context_t *context);
int elf_execute(elf_context_t *context);
int elf_unload(elf_context_t *context);

// ELF parsing functions
int elf_parse_header(const void *data, elf_context_t *context);
int elf_load_segments(const void *data, elf_context_t *context);
uint32_t elf_get_entry_point(const void *data);

// ELF utilities
const char *elf_get_type_string(uint16_t type);
const char *elf_get_machine_string(uint16_t machine);

#endif // ELF_H
