#include <stdint.h>
#include "heap.h"

#define HEAP_SIZE (256 * 1024)

typedef struct block {
    uint32_t size;
    uint32_t free;
    struct block* next;
} block_t;

static unsigned char heap_area[HEAP_SIZE]
    __attribute__((aligned(16)));

static block_t* head;

void heap_init(void)
{
    head = (block_t*)heap_area;

    head->size = HEAP_SIZE - sizeof(block_t);
    head->free = 1;
    head->next = 0;
}

void* kmalloc(uint32_t size)
{
    if (!size || size > HEAP_SIZE - sizeof(block_t))
        return 0;

    size = (size + 15) & ~15u;

    block_t* current = head;

    while (current) {
        if (current->free && current->size >= size) {
            if (current->size >= size + sizeof(block_t) + 16) {
                block_t* next = (block_t*)
                    ((uint8_t*)current + sizeof(block_t) + size);

                next->size = current->size - size - sizeof(block_t);
                next->free = 1;
                next->next = current->next;

                current->next = next;
                current->size = size;
            }

            current->free = 0;

            return (uint8_t*)current + sizeof(block_t);
        }

        current = current->next;
    }

    return 0;
}

void kfree(void* ptr)
{
    if (!ptr)
        return;

    block_t* current = head;

    while (current) {
        void* data =
            (uint8_t*)current + sizeof(block_t);

        if (data == ptr) {
            if (current->free)
                return;

            current->free = 1;

            while (current->next && current->next->free) {
                current->size +=
                    sizeof(block_t) + current->next->size;

                current->next = current->next->next;
            }

            return;
        }

        current = current->next;
    }
}