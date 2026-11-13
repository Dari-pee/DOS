#include "vga.h"

extern void bios_set_video_mode(unsigned char mode);

void vga_set_mode_13h(void)
{
    bios_set_video_mode(0x13);
}

void vga_set_mode_text(void)
{
    bios_set_video_mode(0x03);
}
