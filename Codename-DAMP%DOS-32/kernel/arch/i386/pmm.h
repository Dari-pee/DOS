#ifndef PMM_H
#define PMM_H

#include <stdint.h>

void pmm_init(uint32_t mmap_addr, uint32_t mmap_count);
void* pmm_alloc_page(void);
void pmm_free_page(void* address);
uint32_t pmm_get_free_pages(void);

#endif