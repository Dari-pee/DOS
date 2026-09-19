#include <stdint.h>
#include "io.h"
#include "mouse.h"

int mouse_x = 160;
int mouse_y = 100;
int mouse_left_button = 0;

static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[3];

static void mouse_wait_write(void)
{
    int timeout = 100000;
    while (timeout-- && (inb(0x64) & 2)) { }
}

static void mouse_wait_read(void)
{
    int timeout = 100000;
    while (timeout-- && !(inb(0x64) & 1)) { }
}

static void mouse_write(uint8_t data)
{
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, data);
}

static uint8_t mouse_read(void)
{
    mouse_wait_read();
    return inb(0x60);
}

void mouse_init(void)
{
    uint8_t status;

    outb(0x64, 0xA8);

    outb(0x64, 0x20);
    status = mouse_read();
    status &= (uint8_t)~0x02;
    status &= (uint8_t)~0x20;
    outb(0x64, 0x60);
    outb(0x60, status);

    mouse_write(0xF6);
    mouse_read();

    mouse_write(0xF4);
    mouse_read();
}

int mouse_poll(void)
{
    uint8_t status = inb(0x64);

    if (!(status & 1))
        return 0;

    /* Keyboard and mouse share port 0x60.  During the graphical desktop
       keyboard input is unused, but it must still be consumed or it will
       remain at the controller head and prevent later mouse packets. */
    if (!(status & 0x20)) {
        (void)inb(0x60);
        return 0;
    }

    mouse_bytes[mouse_cycle++] = inb(0x60);
    if (mouse_cycle < 3)
        return 0;

    mouse_cycle = 0;
    if (!(mouse_bytes[0] & 0x08))
        return 0;

    int dx = mouse_bytes[1];
    int dy = mouse_bytes[2];
    if (mouse_bytes[0] & 0x10) dx -= 256;
    if (mouse_bytes[0] & 0x20) dy -= 256;

    mouse_x += dx;
    mouse_y -= dy;

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > 319) mouse_x = 319;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > 199) mouse_y = 199;

    mouse_left_button = mouse_bytes[0] & 0x01;
    return 1;
}
