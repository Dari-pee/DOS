#include <stdint.h>
#include "kernel_stdio.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct interrupt_frame;

#define IDT_ENTRIES 32
#define KERNEL_CODE_SEG 0x08

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;

static const char *exception_names[IDT_ENTRIES] = {
    "Divide-by-zero", "Debug", "Non-maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 Floating-Point Exception", "Alignment Check",
    "Machine Check", "SIMD Floating-Point Exception", "Virtualization Exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Security Exception", "Reserved"
};

static void idt_set_gate(int num, uint32_t handler)
{
    idt[num].offset_low  = handler & 0xFFFF;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
    idt[num].selector    = KERNEL_CODE_SEG;
    idt[num].zero        = 0;
    idt[num].type_attr   = 0x8E;
}

static void exception_panic(int num, unsigned long error_code, int has_error_code)
{
    __asm__ volatile ("cli");
    printf("\n\n*** KERNEL PANIC ***\n");
    printf("Exception %d: %s\n", num, exception_names[num]);
    if (has_error_code)
        printf("Error code: %x\n", (unsigned int)error_code);
    printf("System halted.\n");
    for (;;)
        __asm__ volatile ("hlt");
}

#define EXCEPTION_NOERR(name, num) \
    __attribute__((interrupt)) static void name(struct interrupt_frame *frame) { \
        (void)frame; exception_panic(num, 0, 0); }

#define EXCEPTION_ERR(name, num) \
    __attribute__((interrupt)) static void name(struct interrupt_frame *frame, unsigned long error_code) { \
        (void)frame; exception_panic(num, error_code, 1); }

EXCEPTION_NOERR(isr0, 0)
EXCEPTION_NOERR(isr1, 1)
EXCEPTION_NOERR(isr2, 2)
EXCEPTION_NOERR(isr3, 3)
EXCEPTION_NOERR(isr4, 4)
EXCEPTION_NOERR(isr5, 5)
EXCEPTION_NOERR(isr6, 6)
EXCEPTION_NOERR(isr7, 7)
EXCEPTION_ERR  (isr8, 8)
EXCEPTION_NOERR(isr9, 9)
EXCEPTION_ERR  (isr10, 10)
EXCEPTION_ERR  (isr11, 11)
EXCEPTION_ERR  (isr12, 12)
EXCEPTION_ERR  (isr13, 13)
EXCEPTION_ERR  (isr14, 14)
EXCEPTION_NOERR(isr15, 15)
EXCEPTION_NOERR(isr16, 16)
EXCEPTION_ERR  (isr17, 17)
EXCEPTION_NOERR(isr18, 18)
EXCEPTION_NOERR(isr19, 19)
EXCEPTION_NOERR(isr20, 20)
EXCEPTION_NOERR(isr21, 21)
EXCEPTION_NOERR(isr22, 22)
EXCEPTION_NOERR(isr23, 23)
EXCEPTION_NOERR(isr24, 24)
EXCEPTION_NOERR(isr25, 25)
EXCEPTION_NOERR(isr26, 26)
EXCEPTION_NOERR(isr27, 27)
EXCEPTION_NOERR(isr28, 28)
EXCEPTION_NOERR(isr29, 29)
EXCEPTION_NOERR(isr30, 30)
EXCEPTION_NOERR(isr31, 31)

void idt_install(void)
{
    idt_set_gate(0,  (uint32_t)isr0);
    idt_set_gate(1,  (uint32_t)isr1);
    idt_set_gate(2,  (uint32_t)isr2);
    idt_set_gate(3,  (uint32_t)isr3);
    idt_set_gate(4,  (uint32_t)isr4);
    idt_set_gate(5,  (uint32_t)isr5);
    idt_set_gate(6,  (uint32_t)isr6);
    idt_set_gate(7,  (uint32_t)isr7);
    idt_set_gate(8,  (uint32_t)isr8);
    idt_set_gate(9,  (uint32_t)isr9);
    idt_set_gate(10, (uint32_t)isr10);
    idt_set_gate(11, (uint32_t)isr11);
    idt_set_gate(12, (uint32_t)isr12);
    idt_set_gate(13, (uint32_t)isr13);
    idt_set_gate(14, (uint32_t)isr14);
    idt_set_gate(15, (uint32_t)isr15);
    idt_set_gate(16, (uint32_t)isr16);
    idt_set_gate(17, (uint32_t)isr17);
    idt_set_gate(18, (uint32_t)isr18);
    idt_set_gate(19, (uint32_t)isr19);
    idt_set_gate(20, (uint32_t)isr20);
    idt_set_gate(21, (uint32_t)isr21);
    idt_set_gate(22, (uint32_t)isr22);
    idt_set_gate(23, (uint32_t)isr23);
    idt_set_gate(24, (uint32_t)isr24);
    idt_set_gate(25, (uint32_t)isr25);
    idt_set_gate(26, (uint32_t)isr26);
    idt_set_gate(27, (uint32_t)isr27);
    idt_set_gate(28, (uint32_t)isr28);
    idt_set_gate(29, (uint32_t)isr29);
    idt_set_gate(30, (uint32_t)isr30);
    idt_set_gate(31, (uint32_t)isr31);

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idtp));
}