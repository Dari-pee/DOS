#include <stdint.h>
#include "pmm.h"

#define PAGE_SIZE 4096
#define MAX_PAGES 1048576
#define BITMAP_WORDS (MAX_PAGES / 32)

#define E820_USABLE 1

extern uint8_t __kernel_start;
extern uint8_t __kernel_end;

struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_attr;
} __attribute__((packed));

static uint32_t bitmap[BITMAP_WORDS];
static uint32_t free_pages = 0;

static void set_used(uint32_t page)
{
    if (bitmap[page / 32] & (1u << (page % 32)))
        free_pages--;

    bitmap[page / 32] &= ~(1u << (page % 32));
}

static void set_free(uint32_t page)
{
    if (!(bitmap[page / 32] & (1u << (page % 32)))) {
        bitmap[page / 32] |= 1u << (page % 32);
        free_pages++;
    }
}

void pmm_init(uint32_t mmap_addr, uint32_t mmap_count)
{
    for (uint32_t i = 0; i < BITMAP_WORDS; i++)
        bitmap[i] = 0;

    free_pages = 0;

    struct e820_entry* map =
        (struct e820_entry*)mmap_addr;

    for (uint32_t i = 0; i < mmap_count; i++) {
        if (map[i].type != E820_USABLE)
            continue;

        uint64_t start = map[i].base;
        uint64_t end = start + map[i].length;

        if (end < start)
            end = 0x100000000ULL;

        if (start >= 0x100000000ULL)
            continue;

        if (end > 0x100000000ULL)
            end = 0x100000000ULL;

        uint32_t first =
            (uint32_t)((start + PAGE_SIZE - 1) / PAGE_SIZE);

        uint32_t last =
            (uint32_t)(end / PAGE_SIZE);

        for (uint32_t p = first; p < last; p++)
            set_free(p);
    }

    /* Reserve the first 1 MiB. */
    for (uint32_t p = 0; p < 256; p++)
        set_used(p);

    /* Reserve the complete kernel image. */
    uint32_t kernel_start =
        (uint32_t)&__kernel_start;

    uint32_t kernel_end =
        (uint32_t)&__kernel_end;

    uint32_t first =
        kernel_start / PAGE_SIZE;

    uint32_t last =
        (kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t p = first; p < last; p++)
        set_used(p);
}

void* pmm_alloc_page(void)
{
    for (uint32_t i = 0; i < BITMAP_WORDS; i++) {
        uint32_t word = bitmap[i];

        if (!word)
            continue;

        for (uint32_t bit = 0; bit < 32; bit++) {
            if (word & (1u << bit)) {
                uint32_t page = i * 32 + bit;

                set_used(page);

                return (void*)(page * PAGE_SIZE);
            }
        }
    }

    return 0;
}

void pmm_free_page(void* address)
{
    uint32_t addr = (uint32_t)address;

    if (addr % PAGE_SIZE)
        return;

    uint32_t page = addr / PAGE_SIZE;

    if (page < 256 || page >= MAX_PAGES)
        return;

    set_free(page);
}

uint32_t pmm_get_free_pages(void)
{
    return free_pages;
}