/**
 * TocinOS Memory Management Header
 */

#ifndef MEMORY_H
#define MEMORY_H

// Physical Memory Manager (PMM)
void pmm_init(void);
unsigned int pmm_alloc_page(void);
void pmm_free_page(unsigned int address);
void pmm_set_page_used(unsigned int page);
unsigned int pmm_get_total_pages(void);
unsigned int pmm_get_used_pages(void);
unsigned int pmm_get_free_pages(void);

// Virtual Memory Manager (VMM)
void vmm_init(void);
void vmm_map_page(unsigned int virtual_addr, unsigned int physical_addr, unsigned int flags);
void vmm_unmap_page(unsigned int virtual_addr);
void vmm_switch_directory(unsigned int directory_phys);
unsigned int vmm_get_current_directory(void);

#endif // MEMORY_H
