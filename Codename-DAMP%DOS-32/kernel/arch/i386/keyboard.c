#include <stdint.h>
#include "pic.h"
#include "idt.h"

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

#define KBD_BUF_SIZE 64

static volatile uint8_t kbd_buf[KBD_BUF_SIZE];
static volatile uint8_t kbd_head = 0;     /* written by the interrupt handler */
static volatile uint8_t kbd_tail = 0;     /* written by keyboard_getchar */

struct interrupt_frame;

__attribute__((interrupt)) void keyboard_isr(struct interrupt_frame *frame)
{
    (void)frame;
    uint8_t scancode = inb(0x60);
    uint8_t next = (kbd_head + 1) % KBD_BUF_SIZE;

    if (next != kbd_tail) {
        kbd_buf[kbd_head] = scancode;
        kbd_head = next;
    }

    pic_eoi(1);
}

static int kbd_pop(uint8_t *out)
{
    if (kbd_head == kbd_tail)
        return 0;
    *out = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return 1;
}

void keyboard_init(void)
{
    while (inb(0x64) & 1)
        inb(0x60);

    idtsh(33, keyboard_isr);
    pic_unmask(1);
}

static int shift = 0;

static const char keymap[2][128] = {
    {
        [0x02]='1',[0x03]='2',[0x04]='3',
        [0x05]='4',[0x06]='5',[0x07]='6',
        [0x08]='7',[0x09]='8',[0x0A]='9',
        [0x0B]='0',[0x0C]='-',[0x0D]='=',
        [0x10]='q',[0x11]='w',[0x12]='e',
        [0x13]='r',[0x14]='t',[0x15]='y',
        [0x16]='u',[0x17]='i',[0x18]='o',
        [0x19]='p',[0x1A]='[',[0x1B]=']',
        [0x1E]='a',[0x1F]='s',[0x20]='d',
        [0x21]='f',[0x22]='g',[0x23]='h',
        [0x24]='j',[0x25]='k',[0x26]='l',
        [0x27]=';',[0x28]='\'',
        [0x2C]='z',[0x2D]='x',[0x2E]='c',
        [0x2F]='v',[0x30]='b',[0x31]='n',
        [0x32]='m',[0x33]=',',[0x34]='.',
        [0x35]='/',[0x39]=' '
    },
    {
        [0x02]='!',[0x03]='@',[0x04]='#',
        [0x05]='$',[0x06]='%',[0x07]='^',
        [0x08]='&',[0x09]='*',[0x0A]='(',
        [0x0B]=')',[0x0C]='_',[0x0D]='+',
        [0x10]='Q',[0x11]='W',[0x12]='E',
        [0x13]='R',[0x14]='T',[0x15]='Y',
        [0x16]='U',[0x17]='I',[0x18]='O',
        [0x19]='P',[0x1A]='{',[0x1B]='}',
        [0x1E]='A',[0x1F]='S',[0x20]='D',
        [0x21]='F',[0x22]='G',[0x23]='H',
        [0x24]='J',[0x25]='K',[0x26]='L',
        [0x27]=':',[0x28]='"',
        [0x2C]='Z',[0x2D]='X',[0x2E]='C',
        [0x2F]='V',[0x30]='B',[0x31]='N',
        [0x32]='M',[0x33]='<',[0x34]='>',
        [0x35]='?',[0x39]=' '
    }
};

char keyboard_getchar(void)
{
        uint8_t scancode;

        while (kbd_pop(&scancode))
        {   

        if (scancode == 0xE0)
            continue;

        if (scancode == 0x2A || scancode == 0x36)
        {
            shift = 1;
            continue;
        }

        if (scancode == 0xAA || scancode == 0xB6)
        {
            shift = 0;
            continue;
        }

        if (scancode & 0x80)
            continue;

        if (scancode == 0x1C)
            return '\n';

        if (scancode == 0x0E)
            return '\b';

        if (scancode == 0x0F)
            return '\t';

        return keymap[shift][scancode];
    }

    return 0;
}