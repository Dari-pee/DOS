#include <stdarg.h>
#include "kernel_stdio.h"
#include "idt.h"
#include "vga.h"
#include "mouse.h"
#include "disk.h"

extern void desktop_draw(void);
extern void desktop_draw_start_menu(void);

#define DESKTOP_BUTTON_X 296
#define DESKTOP_BUTTON_Y 4
#define DESKTOP_BUTTON_W 16
#define DESKTOP_BUTTON_H 16
#define START_BUTTON_X 6
#define START_BUTTON_Y 182
#define START_BUTTON_W 54
#define START_BUTTON_H 14

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile char *)0xB8000)
#define DEFAULT_COLOR 0x07
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define DIRECTORY_BUFFER_ADDRESS 0xE000
#define DIRECTORY_ENTRIES 16

#define BPB_BASE 0x7C00
#define FAT_BUFFER_ADDRESS 0xA000

static const unsigned char *const bpb = (const unsigned char *)BPB_BASE;

static unsigned short fat12_data_start_sector;
static unsigned short fat12_root_directory_entries;
static volatile const unsigned char *fat12_root_directory;

static unsigned short bpb_word(int offset)
{
    return (unsigned short)(bpb[offset] | (bpb[offset + 1] << 8));
}

static void fat12_layout_init(void)
{
    unsigned short reserved_sectors = bpb_word(0x0E);
    unsigned char  num_fats         = bpb[0x10];
    unsigned short sectors_per_fat  = bpb_word(0x16);
    unsigned short root_entries     = bpb_word(0x11);
    unsigned short fat_start_sector;
    unsigned short root_start_sector;
    unsigned short root_dir_sectors;

    fat_start_sector  = reserved_sectors;
    root_start_sector = (unsigned short)(fat_start_sector + num_fats * sectors_per_fat);
    root_dir_sectors  = (unsigned short)((root_entries * 32 + 511) / 512);

    fat12_data_start_sector      = (unsigned short)(root_start_sector + root_dir_sectors);
    fat12_root_directory_entries = root_entries;

    /* bootwo.asm places the root directory buffer right after its FAT
       buffer, sized to whatever this FAT actually is. */
    fat12_root_directory = (volatile const unsigned char *)
        (FAT_BUFFER_ADDRESS + (unsigned int)sectors_per_fat * 512);
}
static volatile unsigned char *const directory_buffer =
    (volatile unsigned char *)DIRECTORY_BUFFER_ADDRESS;
static volatile const unsigned char *current_directory;
static int current_directory_entries;
static unsigned short current_directory_cluster;

static int cursor_x;
static int cursor_y;

static void print_char_raw(char character, int x, int y, char attribute)
{
    int offset = (y * VGA_WIDTH + x) * 2;
    VGA_MEMORY[offset] = character;
    VGA_MEMORY[offset + 1] = attribute;
}

static void scroll_screen(void)
{
    int x;
    int y;

    for (y = 1; y < VGA_HEIGHT; y++) {
        for (x = 0; x < VGA_WIDTH * 2; x++)
            VGA_MEMORY[(y - 1) * VGA_WIDTH * 2 + x] = VGA_MEMORY[y * VGA_WIDTH * 2 + x];
    }
    for (x = 0; x < VGA_WIDTH; x++)
        print_char_raw(' ', x, VGA_HEIGHT - 1, DEFAULT_COLOR);
    cursor_y = VGA_HEIGHT - 1;
}

static void terminal_clear(void)
{
    int x;
    int y;

    for (y = 0; y < VGA_HEIGHT; y++) {
        for (x = 0; x < VGA_WIDTH; x++)
            print_char_raw(' ', x, y, DEFAULT_COLOR);
    }
    cursor_x = 0;
    cursor_y = 0;
}

static void terminal_backspace(void)
{
    if (cursor_x > 0) {
        cursor_x--;
        print_char_raw(' ', cursor_x, cursor_y, DEFAULT_COLOR);
    }
}

static void terminal_write_char(char character)
{
    if (character == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (character == '\r') {
        cursor_x = 0;
    } else {
        print_char_raw(character, cursor_x, cursor_y, DEFAULT_COLOR);
        cursor_x++;
    }
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= VGA_HEIGHT)
        scroll_screen();
}

static void terminal_write_string(const char *text)
{
    while (*text != '\0')
        terminal_write_char(*text++);
}

static unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static char keyboard_getchar(void)
{
    static const char scan_codes[128] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
        '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
        'o', 'p', '[', ']', '\n', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j',
        'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
        'm', ',', '.', '/', 0, '*', 0, ' '
    };
    unsigned char scan_code;
    unsigned char status;

    for (;;) {
        while (((status = inb(KEYBOARD_STATUS_PORT)) & 1) == 0) {
        }
        scan_code = inb(KEYBOARD_DATA_PORT);

        /* PS/2 keyboard and mouse share port 0x60.  Never interpret a
           mouse packet as a keyboard scan code after leaving the desktop. */
        if ((status & 0x20) != 0)
            continue;

        if ((scan_code & 0x80) == 0 && scan_code < sizeof(scan_codes))
            return scan_codes[scan_code];
    }
}

static int print_unsigned(unsigned int value, unsigned int base)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[16];
    int length = 0;

    if (value == 0) {
        terminal_write_char('0');
        return 1;
    }
    while (value != 0) {
        buffer[length++] = digits[value % base];
        value /= base;
    }
    while (length > 0)
        terminal_write_char(buffer[--length]);
    return 0;
}

int printf(const char *format, ...)
{
    va_list arguments;
    int written = 0;

    va_start(arguments, format);
    while (*format != '\0') {
        if (*format != '%') {
            terminal_write_char(*format++);
            written++;
            continue;
        }
        format++;
        if (*format == '%') {
            terminal_write_char('%');
            format++;
            written++;
        } else if (*format == 'c') {
            terminal_write_char((char)va_arg(arguments, int));
            format++;
            written++;
        } else if (*format == 's') {
            const char *text = va_arg(arguments, const char *);
            while (*text != '\0') {
                terminal_write_char(*text++);
                written++;
            }
            format++;
        } else if (*format == 'd') {
            int value = va_arg(arguments, int);
            if (value < 0) {
                terminal_write_char('-');
                value = -value;
                written++;
            }
            print_unsigned((unsigned int)value, 10);
            format++;
        } else if (*format == 'u') {
            print_unsigned(va_arg(arguments, unsigned int), 10);
            format++;
        } else if (*format == 'x') {
            print_unsigned(va_arg(arguments, unsigned int), 16);
            format++;
        } else {
            terminal_write_char('%');
            written++;
        }
    }
    va_end(arguments);
    return written;
}

static int read_line(char *buffer, int capacity)
{
    int length = 0;

    for (;;) {
        char character = keyboard_getchar();
        if (character == 0 || character == '\t')
            continue;
        if (character == '\b') {
            if (length > 0) {
                length--;
                terminal_backspace();
            }
        } else if (character == '\n') {
            terminal_write_char('\n');
            buffer[length] = '\0';
            return length;
        } else if (length < capacity - 1) {
            buffer[length++] = character;
            terminal_write_char(character);
        }
    }
}

int scanf(const char *format, ...)
{
    char input[128];
    const char *text;
    int assigned = 0;
    va_list arguments;

    read_line(input, sizeof(input));
    text = input;
    va_start(arguments, format);
    while (*format != '\0') {
        int width = 0;
        if (*format == ' ') {
            while (*format == ' ')
                format++;
            while (*text == ' ')
                text++;
            continue;
        }
        if (*format != '%') {
            if (*text++ != *format++)
                break;
            continue;
        }
        format++;
        while (*format >= '0' && *format <= '9')
            width = width * 10 + (*format++ - '0');
        if (*format == 'd') {
            int sign = 1;
            int value = 0;
            int *destination = va_arg(arguments, int *);
            while (*text == ' ')
                text++;
            if (*text == '-') {
                sign = -1;
                text++;
            }
            if (*text < '0' || *text > '9')
                break;
            while (*text >= '0' && *text <= '9')
                value = value * 10 + (*text++ - '0');
            *destination = value * sign;
            assigned++;
        } else if (*format == 'c') {
            char *destination = va_arg(arguments, char *);
            if (*text == '\0')
                break;
            *destination = *text++;
            assigned++;
        } else if (*format == 's') {
            char *destination = va_arg(arguments, char *);
            int length = 0;
            if (width == 0)
                width = 127;
            while (*text == ' ')
                text++;
            while (*text != '\0' && *text != ' ' && length < width - 1)
                destination[length++] = *text++;
            destination[length] = '\0';
            if (length == 0)
                break;
            assigned++;
        } else {
            break;
        }
        format++;
    }
    va_end(arguments);
    return assigned;
}

static int strings_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (*left != *right)
            return 0;
        left++;
        right++;
    }
    return *left == *right;
}

static int starts_with(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text++ != *prefix++)
            return 0;
    }
    return 1;
}

static unsigned char cursor_backing[36];
static int cursor_last_x = -100;
static int cursor_last_y = -100;
static int cursor_visible = 0;

static void restore_under_cursor(void)
{
    volatile unsigned char *screen = (volatile unsigned char *)0xA0000;
    int dx, dy, i = 0;

    if (!cursor_visible)
        return;

    for (dy = 0; dy < 6; dy++) {
        for (dx = 0; dx < 6; dx++) {
            int px = cursor_last_x + dx;
            int py = cursor_last_y + dy;
            if (px >= 0 && px < 320 && py >= 0 && py < 200)
                screen[py * 320 + px] = cursor_backing[i];
            i++;
        }
    }
}

static void draw_cursor(int x, int y)
{
    volatile unsigned char *screen = (volatile unsigned char *)0xA0000;
    int dx, dy, i = 0;

    for (dy = 0; dy < 6; dy++) {
        for (dx = 0; dx < 6; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < 320 && py >= 0 && py < 200) {
                cursor_backing[i] = screen[py * 320 + px];
                screen[py * 320 + px] = 0;
            }
            i++;
        }
    }

    cursor_last_x = x;
    cursor_last_y = y;
    cursor_visible = 1;
}

static void enter_desktop(void)
{
    /* If the close click launched this desktop loop again before its release
       packet arrived, do not treat that same held button as a new click. */
    int previous_button_state = mouse_left_button;
    int start_menu_open = 0;

    vga_set_mode_13h();
    desktop_draw();
    cursor_visible = 0;
    draw_cursor(mouse_x, mouse_y);

    for (;;) {
        /* Redraw only after a complete mouse packet changes the position.
           Erasing and repainting on every polling iteration caused flicker. */
        if (mouse_poll()) {
            restore_under_cursor();

            if (mouse_left_button && !previous_button_state) {
                int over_close_button =
                    mouse_x >= DESKTOP_BUTTON_X && mouse_x < DESKTOP_BUTTON_X + DESKTOP_BUTTON_W &&
                    mouse_y >= DESKTOP_BUTTON_Y && mouse_y < DESKTOP_BUTTON_Y + DESKTOP_BUTTON_H;
                int over_start_button =
                    mouse_x >= START_BUTTON_X && mouse_x < START_BUTTON_X + START_BUTTON_W &&
                    mouse_y >= START_BUTTON_Y && mouse_y < START_BUTTON_Y + START_BUTTON_H;

                if (over_close_button)
                    break;

                if (over_start_button) {
                    start_menu_open = !start_menu_open;
                    desktop_draw();
                    if (start_menu_open)
                        desktop_draw_start_menu();
                }
            }

            draw_cursor(mouse_x, mouse_y);
            previous_button_state = mouse_left_button;
        }
    }

    vga_set_mode_text();
    terminal_clear();
    terminal_write_string("Back in DOS-32 shell.\n");
}

static int fat12_load_directory(unsigned short cluster)
{
    unsigned short sector;

    if (cluster == 0) {
        current_directory = fat12_root_directory;
        current_directory_entries = fat12_root_directory_entries;
        current_directory_cluster = 0;
        return 1;
    }

    /* The current FAT12 image uses one sector per cluster.  This reads the
       first cluster of a directory; its small starter folders fit in it. */
    sector = fat12_data_start_sector + cluster - 2;
    if (bios_read_sector(sector, (void *)DIRECTORY_BUFFER_ADDRESS) != 0)
        return 0;

    current_directory = directory_buffer;
    current_directory_entries = DIRECTORY_ENTRIES;
    current_directory_cluster = cluster;
    return 1;
}

static int fat12_directory_named(const char *name, unsigned short *cluster)
{
    int i;

    for (i = 0; i < current_directory_entries; i++) {
        volatile const unsigned char *entry = current_directory + i * 32;
        int j;
        int matches = 1;
        int name_ended = 0;

        if (entry[0] == 0x00)
            break;
        if (entry[0] == 0xE5 || (entry[11] & 0x10) == 0)
            continue;

        for (j = 0; j < 8; j++) {
            char character = name_ended ? ' ' : name[j];
            if (character == '\0') {
                name_ended = 1;
                character = ' ';
            }
            if (character >= 'a' && character <= 'z')
                character -= 'a' - 'A';
            if (entry[j] != (unsigned char)character) {
                matches = 0;
                break;
            }
        }
        if (matches && (name_ended || name[8] == '\0')) {
            *cluster = (unsigned short)(entry[26] | (entry[27] << 8));
            return 1;
        }
    }
    return 0;
}

static void cd_command(const char *path)
{
    unsigned short cluster;

    if (*path == '\0') {
        terminal_write_string("Usage: cd <folder>\n");
        return;
    }

    if (strings_equal(path, "..")) {
        if (current_directory_cluster == 0) {
            terminal_write_string("Already at root.\n");
            return;
        }
        /* FAT12's second entry is '..'; a root parent has cluster zero. */
        cluster = (unsigned short)(current_directory[32 + 26] |
                                   (current_directory[32 + 27] << 8));
        if (!fat12_load_directory(cluster))
            terminal_write_string("Disk read failed.\n");
        return;
    }

    if (!fat12_directory_named(path, &cluster)) {
        terminal_write_string("Folder not found: ");
        terminal_write_string(path);
        terminal_write_char('\n');
        return;
    }
    if (!fat12_load_directory(cluster))
        terminal_write_string("Disk read failed.\n");
}

static void dir_command(void)
{
    int found = 0;
    int i;

    terminal_write_char('\n');
    for (i = 0; i < current_directory_entries; i++) {
        volatile const unsigned char *entry = current_directory + i * 32;
        int j;

        if (entry[0] == 0x00)
            break;
        if (entry[0] == 0xE5)       /* Deleted entry. */
            continue;
        if ((entry[11] & 0x0F) == 0x0F) /* Long filename entry. */
            continue;
        if ((entry[11] & 0x08) != 0) /* Volume label. */
            continue;
        if (entry[0] == '.') /* Hide FAT12's . and .. entries. */
            continue;

        terminal_write_string("  ");
        for (j = 0; j < 8; j++) {
            if (entry[j] != ' ')
                terminal_write_char(entry[j]);
        }
        if (entry[8] != ' ') {
            terminal_write_char('.');
            for (j = 8; j < 11; j++) {
                if (entry[j] != ' ')
                    terminal_write_char(entry[j]);
            }
        }
        if ((entry[11] & 0x10) != 0)
            terminal_write_char('/');
        terminal_write_char('\n');
        found = 1;
    }

    if (!found)
        terminal_write_string("  <empty>\n");
}

static void print_prompt(void)
{
    terminal_write_string("DOS-32> ");
}

static void run_command(const char *command)
{
    if (command[0] == '\0') {
        return;
    } else if (strings_equal(command, "help")) {
        terminal_write_string("Commands: help, clear, dir, cd, echo, about, exit, desktop.\n");
    
    } else if (strings_equal(command, "exit")) {
        terminal_write_string("Exiting DOS-32...\n");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    
    } else if (strings_equal(command, "desktop")) {
    enter_desktop();

    } else if (strings_equal(command, "canttakemyeyesoffyou")) {
        terminal_write_string("I love you baby, and if it's quite alright\n");
        terminal_write_string("I need you baby to warm the lonely nights\n");
        terminal_write_string("I love you baby, trust in me when I say\n");
        terminal_write_string("Oh pretty baby, don't bring me down I pray\n");
        terminal_write_string("Oh pretty baby, now that I found you, stay\n");
        terminal_write_string("And let me love you baby, let me love you\n");
        terminal_write_string("\n");
    
    } else if (strings_equal(command, "allyoubasebelongstous")) {
        terminal_write_string("All your base are belong to us\n");
        terminal_write_string("You have no chance to survive make your time\n");
        terminal_write_string("Ha ha ha ha...\n");
        terminal_write_string("\n");
    
    } else if (strings_equal(command, "howareyougithubcopilot")) { // copilot helped me create these command lmao
        terminal_write_string("I am fine, thank you for asking.\n");
        terminal_write_string("How are you?\n");
        terminal_write_string("\n");

    } else if (strings_equal(command, "copilotisawesome")) {
        terminal_write_string("Yes, GitHub Copilot is awesome!\n");

    } else if (strings_equal(command, "copilotsucks")) {
        terminal_write_string("shutting down OS\n");
        for (;;) {
            __asm__ volatile ("hlt");
        }

    } else if (strings_equal(command, "thisisaverylongcommandthatdoesnotexist")) {
        terminal_write_string("This is a very long command that does not exist.\n");
        terminal_write_string("Please try a different command.\n");
        terminal_write_string("\n");
    
    } else if (strings_equal(command, "thisisaverylongcommandthatdoesnotexistbutitmightexist")) {
        terminal_write_string("This is a very long command that does not exist but it might exist.\n");
        terminal_write_string("Please try a different command.\n");
        terminal_write_string("\n");
    
    } else if (strings_equal(command, "thisisaverylongcommandthatdoesnotexistanditwillneverexist")) {
        terminal_write_string("This is a very long command that does not exist and it will never exist.\n");
        terminal_write_string("Please try a different command.\n");
        terminal_write_string("\n");

    } else if(strings_equal(command, "istheOSai")) {
        terminal_write_string("no pure human labor\n");
        terminal_write_string("\n");
    
    } else if (strings_equal(command, "dir")) {
        dir_command();
    } else if (starts_with(command, "cd ")) {
        cd_command(command + 3);
    } else if (strings_equal(command, "cd")) {
        terminal_write_string("Usage: cd <folder>\n");
    } else if (strings_equal(command, "clear")) {
        terminal_clear();
    } else if (starts_with(command, "echo ")) {
        terminal_write_string(command + 5);
        terminal_write_char('\n');
    } else if (strings_equal(command, "echo")) {
        terminal_write_char('\n');
    } else if (strings_equal(command, "about")) {
        terminal_write_string("DOS-32 is a 32-bit protected-mode hobby OS created by Dari.\n");
    } else {
        terminal_write_string("Unknown command: ");
        terminal_write_string(command);
        terminal_write_string("\nType 'help' for commands.\n");
    }
}
void kernel_main(void)
{
    idt_install();
    mouse_init();

    fat12_layout_init();
    current_directory = fat12_root_directory;
    current_directory_entries = fat12_root_directory_entries;
    current_directory_cluster = 0;

    terminal_clear();
    terminal_write_string("DOS-32: microsoft pls dont sue me :)\n");

    for (;;) {
        char command[128];

        print_prompt();
        read_line(command, sizeof(command));
        run_command(command);
    }
}
