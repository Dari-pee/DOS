#include <stdint.h>
#include "dma.h"

#define DMA_POOL_START 0x40000u
#define DMA_POOL_END   0x80000u

static uint32_t dma_next = DMA_POOL_START;
void *dma_alloc(uint32_t size, uint32_t align)
{
    uint32_t addr = (dma_next + align - 1) & ~(align - 1);

    if (addr + size > DMA_POOL_END)
        return 0;

    dma_next = addr + size;
    for (uint32_t i = 0; i < size; i++)
        ((volatile uint8_t *)addr)[i] = 0;

    return (void *)addr;
}
