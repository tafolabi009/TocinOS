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

// Dynamic section tags (DT_*)
#define DT_NULL         0   // End of dynamic section
#define DT_NEEDED       1   // Name of needed library
#define DT_PLTRELSZ     2   // Size of PLT relocations
#define DT_PLTGOT       3   // Address of PLT/GOT
#define DT_HASH         4   // Address of symbol hash table
#define DT_STRTAB       5   // Address of string table
#define DT_SYMTAB       6   // Address of symbol table
#define DT_RELA         7   // Address of Rela relocations
#define DT_RELASZ       8   // Total size of Rela relocations
#define DT_RELAENT      9   // Size of one Rela relocation
#define DT_STRSZ        10  // Total size of string table
#define DT_SYMENT       11  // Size of one symbol table entry
#define DT_INIT         12  // Address of init function
#define DT_FINI         13  // Address of fini function
#define DT_SONAME       14  // Shared object name
#define DT_RPATH        15  // Library search path
#define DT_SYMBOLIC     16  // Symbolic linking
#define DT_REL          17  // Address of Rel relocations
#define DT_RELSZ        18  // Total size of Rel relocations
#define DT_RELENT       19  // Size of one Rel relocation
#define DT_PLTREL       20  // Type of PLT relocations
#define DT_DEBUG        21  // Debug info
#define DT_TEXTREL      22  // Text relocations exist
#define DT_JMPREL       23  // Address of PLT relocations
#define DT_BIND_NOW     24  // Process all relocations at load
#define DT_INIT_ARRAY   25  // Array of init functions
#define DT_FINI_ARRAY   26  // Array of fini functions
#define DT_INIT_ARRAYSZ 27  // Size of init array
#define DT_FINI_ARRAYSZ 28  // Size of fini array
#define DT_RUNPATH      29  // Library search path
#define DT_FLAGS        30  // Flags
#define DT_GNU_HASH     0x6ffffef5  // GNU hash table

// Relocation types for i386
#define R_386_NONE      0   // No relocation
#define R_386_32        1   // Direct 32-bit
#define R_386_PC32      2   // PC relative 32-bit
#define R_386_GOT32     3   // GOT entry
#define R_386_PLT32     4   // PLT address
#define R_386_COPY      5   // Copy symbol
#define R_386_GLOB_DAT  6   // Create GOT entry
#define R_386_JMP_SLOT  7   // Create PLT entry
#define R_386_RELATIVE  8   // Adjust by base
#define R_386_GOTOFF    9   // Offset to GOT
#define R_386_GOTPC     10  // PC relative to GOT
#define R_386_TLS_TPOFF 14  // TLS offset

// Symbol binding and type
#define STB_LOCAL       0   // Local symbol
#define STB_GLOBAL      1   // Global symbol
#define STB_WEAK        2   // Weak symbol

#define STT_NOTYPE      0   // No type
#define STT_OBJECT      1   // Data object
#define STT_FUNC        2   // Function
#define STT_SECTION     3   // Section
#define STT_FILE        4   // File

// Macros for symbol info
#define ELF32_ST_BIND(i)    ((i) >> 4)
#define ELF32_ST_TYPE(i)    ((i) & 0xf)
#define ELF32_ST_INFO(b,t)  (((b) << 4) | ((t) & 0xf))

// Macros for relocation info
#define ELF32_R_SYM(i)      ((i) >> 8)
#define ELF32_R_TYPE(i)     ((uint8_t)(i))
#define ELF32_R_INFO(s,t)   (((s) << 8) | (uint8_t)(t))

// Auxiliary vector types
#define AT_NULL         0   // End of vector
#define AT_IGNORE       1   // Ignore entry
#define AT_EXECFD       2   // File descriptor of program
#define AT_PHDR         3   // Program headers address
#define AT_PHENT        4   // Size of program header entry
#define AT_PHNUM        5   // Number of program headers
#define AT_PAGESZ       6   // Page size
#define AT_BASE         7   // Interpreter base address
#define AT_FLAGS        8   // Flags
#define AT_ENTRY        9   // Program entry point
#define AT_NOTELF       10  // Not ELF
#define AT_UID          11  // Real UID
#define AT_EUID         12  // Effective UID
#define AT_GID          13  // Real GID
#define AT_EGID         14  // Effective GID
#define AT_PLATFORM     15  // Platform string
#define AT_HWCAP        16  // Hardware capabilities
#define AT_CLKTCK       17  // Clock ticks per second
#define AT_RANDOM       25  // Address of 16 random bytes
#define AT_EXECFN       31  // File name of executable

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

// ELF32 symbol table entry
typedef struct {
    uint32_t st_name;             // Symbol name (string table index)
    uint32_t st_value;            // Symbol value
    uint32_t st_size;             // Symbol size
    uint8_t  st_info;             // Symbol type and binding
    uint8_t  st_other;            // Symbol visibility
    uint16_t st_shndx;            // Section index
} __attribute__((packed)) elf32_sym_t;

// ELF32 relocation entry (without addend)
typedef struct {
    uint32_t r_offset;            // Address of relocation
    uint32_t r_info;              // Relocation type and symbol index
} __attribute__((packed)) elf32_rel_t;

// ELF32 relocation entry (with addend)
typedef struct {
    uint32_t r_offset;            // Address of relocation
    uint32_t r_info;              // Relocation type and symbol index
    int32_t  r_addend;            // Addend
} __attribute__((packed)) elf32_rela_t;

// ELF32 dynamic section entry
typedef struct {
    int32_t  d_tag;               // Dynamic entry type
    union {
        uint32_t d_val;           // Integer value
        uint32_t d_ptr;           // Address value
    } d_un;
} __attribute__((packed)) elf32_dyn_t;

// Auxiliary vector entry
typedef struct {
    uint32_t a_type;              // Entry type
    union {
        uint32_t a_val;           // Integer value
        void    *a_ptr;           // Pointer value
    } a_un;
} elf32_auxv_t;

// Dynamic linker context
typedef struct {
    // Loaded ELF info
    void *elf_data;               // ELF file data
    uint32_t elf_size;            // ELF file size
    uint32_t load_base;           // Base address where ELF is loaded
    
    // Dynamic section pointers
    elf32_dyn_t *dynamic;         // Dynamic section
    char *strtab;                 // String table
    elf32_sym_t *symtab;          // Symbol table
    uint32_t *hash;               // Symbol hash table
    
    // Relocation tables
    elf32_rel_t *rel;             // REL relocations
    uint32_t rel_count;           // Number of REL entries
    elf32_rel_t *plt_rel;         // PLT relocations
    uint32_t plt_rel_count;       // Number of PLT REL entries
    
    // GOT/PLT
    uint32_t *got;                // Global Offset Table
    uint32_t *plt;                // Procedure Linkage Table
    
    // Init/Fini
    void (*init_func)(void);      // Initialization function
    void (*fini_func)(void);      // Finalization function
    void (**init_array)(void);    // Init function array
    void (**fini_array)(void);    // Fini function array
    uint32_t init_array_sz;       // Size of init array
    uint32_t fini_array_sz;       // Size of fini array
    
    // Interpreter
    char interp_path[256];        // Path to interpreter (ld.so)
    int needs_interp;             // 1 if needs interpreter
    
    // Shared library info
    uint32_t soname_offset;       // SONAME string offset
} elf_dyn_context_t;

// ELF loader context (extended)
typedef struct {
    void *elf_data;               // ELF file data
    uint32_t elf_size;            // ELF file size
    uint32_t entry_point;         // Entry point address
    uint32_t load_base;           // Load base address
    uint32_t is_64bit;            // 1 if 64-bit ELF
    uint32_t is_dynamic;          // 1 if dynamically linked
    
    // Program headers
    elf32_phdr_t *phdr;           // Program header table
    uint32_t phnum;               // Number of program headers
    uint32_t phent;               // Size of program header entry
    
    // Dynamic linking info
    elf_dyn_context_t dyn;        // Dynamic linking context
} elf_context_t;

// ELF loader API
int elf_init(void);
int elf_validate(const void *data, uint32_t size);
int elf_load(const void *data, uint32_t size, elf_context_t *context);
int elf_load_file(const char *path, elf_context_t *context);
int elf_execute(elf_context_t *context);
int elf_unload(elf_context_t *context);

// ELF parsing functions
int elf_parse_header(const void *data, elf_context_t *context);
int elf_load_segments(const void *data, elf_context_t *context);
uint32_t elf_get_entry_point(const void *data);

// Dynamic linking functions
int elf_parse_dynamic(const void *data, elf_context_t *context);
int elf_find_interp(const void *data, elf_context_t *context);
int elf_process_relocations(elf_context_t *context);
void *elf_lookup_symbol(elf_context_t *context, const char *name);
int elf_apply_relocation(elf_context_t *context, elf32_rel_t *rel, uint32_t base);
int elf_call_init(elf_context_t *context);
void elf_call_fini(elf_context_t *context);

// Auxiliary vector functions
int elf_build_auxv(elf_context_t *context, uint32_t *stack, int *argc);

// ELF utilities
const char *elf_get_type_string(uint16_t type);
const char *elf_get_machine_string(uint16_t machine);

#endif // ELF_H
