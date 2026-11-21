#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include "keyboard.h"
#include <kernel/tty.h>
#include "shell.h"
#include "ff.h"
#include "proapi.h"
#include "timer.h"

#define PROGRAM_BASE 0x400000u
#define PROGRAM_MAX  (1024u * 1024u)
#define ELF_LOAD_MIN 0x00400000u
#define ELF_LOAD_MAX 0x01000000u
#define USER_DIR     "0:/USER/"
#define USER_EXT     ".BIN"

#define ELF_PT_LOAD 1
#define ELF_PF_X    1

struct elf32_header {
    unsigned char ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct elf32_program_header {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed));

static struct proapi api;

static int program_write(int fd, const void *buffer, uint32_t count)
{
    if ((fd != 1 && fd != 2) || (!buffer && count))
        return -1;
    const unsigned char *bytes = (const unsigned char *)buffer;
    for (uint32_t i = 0; i < count; i++)
        putchar(bytes[i]);
    return (int)count;
}

static int read_exact(FIL *file, void *buffer, UINT size)
{
    UINT got = 0;
    return f_read(file, buffer, size, &got) == FR_OK && got == size;
}

static FRESULT load_elf(FIL *file, uint32_t file_size, uint32_t *entry_out)
{
    struct elf32_header eh;
    if (f_lseek(file, 0) != FR_OK || !read_exact(file, &eh, sizeof(eh)))
        return FR_INVALID_OBJECT;

    if (eh.ident[0] != 0x7F || eh.ident[1] != 'E' ||
        eh.ident[2] != 'L' || eh.ident[3] != 'F' ||
        eh.ident[4] != 1 || eh.ident[5] != 1 || eh.ident[6] != 1 ||
        eh.type != 2 || eh.machine != 3 || eh.version != 1 ||
        eh.ehsize != sizeof(eh) ||
        eh.phentsize != sizeof(struct elf32_program_header) ||
        eh.phnum == 0 || eh.phnum > 16)
        return FR_INVALID_OBJECT;

    uint32_t phbytes = (uint32_t)eh.phnum * eh.phentsize;
    if (eh.phoff > file_size || phbytes > file_size - eh.phoff)
        return FR_INVALID_OBJECT;

    int entry_is_executable = 0;
    for (uint16_t i = 0; i < eh.phnum; i++) {
        struct elf32_program_header ph;
        uint32_t phpos = eh.phoff + (uint32_t)i * eh.phentsize;
        if (f_lseek(file, phpos) != FR_OK || !read_exact(file, &ph, sizeof(ph)))
            return FR_INVALID_OBJECT;
        if (ph.type != ELF_PT_LOAD)
            continue;
        if (ph.filesz == 0 && ph.memsz == 0)
            continue;

        if (ph.filesz > ph.memsz || ph.offset > file_size ||
            ph.filesz > file_size - ph.offset ||
            ph.vaddr < ELF_LOAD_MIN || ph.vaddr > ELF_LOAD_MAX ||
            ph.memsz > ELF_LOAD_MAX - ph.vaddr)
            return FR_INVALID_OBJECT;

        if (ph.filesz) {
            if (f_lseek(file, ph.offset) != FR_OK ||
                !read_exact(file, (void *)ph.vaddr, ph.filesz))
                return FR_DISK_ERR;
        }
        memset((void *)(ph.vaddr + ph.filesz), 0, ph.memsz - ph.filesz);

        if ((ph.flags & ELF_PF_X) && eh.entry >= ph.vaddr &&
            eh.entry - ph.vaddr < ph.memsz)
            entry_is_executable = 1;
    }

    if (!entry_is_executable)
        return FR_INVALID_OBJECT;
    *entry_out = eh.entry;
    return FR_OK;
}

static void cmd_help(char *args) {
    (void)args;
    printf("Commands: about, cd [directory], clear, echo <text>, help, ls [directory], pwd, cat <file>, write <file> <text>, exec <file.bin|file.elf>, mkdir <directory>, rm <path>, mv <source> <destination>\n");
    printf("If you are Running a Program, Make sure to change your Directory to User, Using cd user. Then run exec <filename> to run the file.\n");
}

static void cmd_write(char *args) {
    if (args == NULL || *args == '\0') {
        printf("Usage: write <file> <text> (replaces the file if it exists)\n");
        return;
    }

    char *text = strchr(args, ' ');
    if (text == NULL) {
        printf("Usage: write <file> <text> (replaces the file if it exists)\n");
        return;
    }

    *text++ = '\0';
    while (*text == ' ' || *text == '\t')
        text++;

    if (*args == '\0' || *text == '\0') {
        printf("Usage: write <file> <text> (replaces the file if it exists)\n");
        return;
    }

    FIL file;
    FRESULT res = f_open(&file, args, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        printf("write: cannot open %s (error %d)\n", args, res);
        return;
    }

    UINT length = (UINT)strlen(text);
    UINT written = 0;
    res = f_write(&file, text, length, &written);
    FRESULT close_res = f_close(&file);

    if (res != FR_OK) {
        printf("write: write failed (error %d)\n", res);
    } else if (written != length) {
        printf("write: incomplete write (%u of %u bytes)\n", written, length);
    } else if (close_res != FR_OK) {
        printf("write: close failed (error %d)\n", close_res);
    } else {
        printf("Wrote %u bytes to %s\n", written, args);
    }
}

static void cmd_rm(char *args) {
    if (args == NULL || *args == '\0') {
        printf("Usage: rm <directory>\n");
        return;
    }

    FRESULT res = f_unlink(args);
    if (res == FR_OK) {
        printf("Removed %s\n", args);
    } else {
        printf("rm: cannot remove %s (error %d)\n", args, res);
    }
}

static void cmd_mv(char *args)
{
    if (args == NULL || *args == '\0') {
        printf("Usage: mv <source> <destination>\n");
        return;
    }

    char *destination = args;
    while (*destination && *destination != ' ' && *destination != '\t')
        destination++;
    if (*destination == '\0') {
        printf("Usage: mv <source> <destination>\n");
        return;
    }

    *destination++ = '\0';
    while (*destination == ' ' || *destination == '\t')
        destination++;
    if (*args == '\0' || *destination == '\0') {
        printf("Usage: mv <source> <destination>\n");
        return;
    }

    FRESULT res = f_rename(args, destination);
    if (res == FR_OK)
        printf("Moved %s to %s\n", args, destination);
    else
        printf("mv: cannot move %s to %s (error %d)\n", args, destination, res);
}

static void cmd_mkdir(char *args) {
    if (args == NULL || *args == '\0') {
        printf("Usage: mkdir <directory>\n");
        return;
    }

    FRESULT res = f_mkdir(args);
    if (res == FR_OK) {
        printf("Created %s\n", args);
    } else {
        printf("mkdir: cannot create %s (error %d)\n", args, res);
    }
}

static void cmd_cd(char *args) {
    if (args != NULL && *args != '\0') {
        FRESULT res = f_chdir(args);
        if (res != FR_OK) {
            printf("cd: cannot change to %s (error %d)\n", args, res);
        }
        return;
    }

    char path[128];
    FRESULT res = f_getcwd(path, sizeof(path));
    if (res == FR_OK)
        printf("%s\n", path);
    else
        printf("cd: cannot read current directory (error %d)\n", res);
}

static void cmd_pwd(char *args) {
    (void)args;
    char path[128];
    FRESULT res = f_getcwd(path, sizeof(path));
    if (res == FR_OK)
        printf("%s\n", path);
    else
        printf("pwd: cannot read current directory (error %d)\n", res);
}

static int build_user_path(const char *name, char *out, size_t out_size)
{
    size_t len = strlen(name);
    size_t dir_len = strlen(USER_DIR);
    size_t ext_len = strlen(USER_EXT);

    if (len == 0 || len > 8)
        return 0;
    if (dir_len + len + ext_len + 1 > out_size)
        return 0;

    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalpha((unsigned char)c) && !isdigit((unsigned char)c) &&
            c != '_' && c != '-')
            return 0;
    }

    size_t pos = 0;
    for (size_t i = 0; i < dir_len; i++)
        out[pos++] = USER_DIR[i];
    for (size_t i = 0; i < len; i++)
        out[pos++] = (char)toupper((unsigned char)name[i]);
    for (size_t i = 0; i < ext_len; i++)
        out[pos++] = USER_EXT[i];
    out[pos] = '\0';
    return 1;
}

static FRESULT load_program(const char *path, uint32_t *entry_out)
{
    FIL file;

    FRESULT res = f_open(&file, path, FA_READ);
    if (res != FR_OK)
        return res;

    FSIZE_t file_size = f_size(&file);
    if (file_size < 4 || file_size > PROGRAM_MAX) {
        f_close(&file);
        return FR_INVALID_OBJECT;
    }

    unsigned char magic[4];
    if (f_lseek(&file, 0) != FR_OK || !read_exact(&file, magic, sizeof(magic))) {
        f_close(&file);
        return FR_DISK_ERR;
    }

    if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
        res = load_elf(&file, (uint32_t)file_size, entry_out);
    } else {
        res = f_lseek(&file, 0);
        if (res == FR_OK && file_size <= PROGRAM_MAX) {
            if (!read_exact(&file, (void *)PROGRAM_BASE, (UINT)file_size))
                res = FR_DISK_ERR;
            else
                *entry_out = PROGRAM_BASE;
        }
    }
    f_close(&file);
    return res;
}

// Jumps into the loaded program, giving it the API and its arguments.
static void start_program(uint32_t entry_address, char *args)
{
    api.printf  = printf;
    api.putchar = putchar;
    api.getkey  = keyboard_getchar;
    api.write   = program_write;
    api.sleep_ms = timer_sleep_ms;
    api.args    = args ? args : "";

    void (*entry)(const struct proapi *) =
        (void (*)(const struct proapi *))entry_address;

    entry(&api);
}

//Run user program in USER folder
static int try_user_program(const char *name, char *args)
{
    char path[32];

    if (!build_user_path(name, path, sizeof(path)))
        return 0;

    uint32_t entry;
    FRESULT res = load_program(path, &entry);
    if (res == FR_NO_FILE || res == FR_NO_PATH)
        return 0;
    if (res != FR_OK) {
        printf("%s: cannot load program (error %d)\n", name, res);
        return 1;
    }

    start_program(entry, args);
    return 1;
}

static int binexten(const char *path)
{
    size_t len = strlen(path);
    if (len < 4)
        return 0;
    const char *ext = path + len - 4;
    return ext[0] == '.' && toupper((unsigned char)ext[1]) == 'B' &&
           toupper((unsigned char)ext[2]) == 'I' &&
           toupper((unsigned char)ext[3]) == 'N';
}

static void cmd_exec(char *args)
{
    if (args == NULL || *args == '\0') {
        printf("Usage: exec <file.bin|file.elf> [arguments]\n");
        return;
    }

    char *rest = strchr(args, ' ');
    if (rest != NULL) {
        *rest++ = '\0';
        while (*rest == ' ')
            rest++;
    }

    size_t arg_len = strlen(args);
    int is_elf = arg_len >= 4 && args[arg_len - 4] == '.' &&
        toupper((unsigned char)args[arg_len - 3]) == 'E' &&
        toupper((unsigned char)args[arg_len - 2]) == 'L' &&
        toupper((unsigned char)args[arg_len - 1]) == 'F';
    if (!binexten(args) && !is_elf) {
        printf("exec: %s is not a .BIN or .ELF program\n", args);
        return;
    }

    uint32_t entry;
    FRESULT res = load_program(args, &entry);
    if (res != FR_OK) {
        printf("exec: cannot load %s (error %d)\n", args, res);
        return;
    }

    start_program(entry, rest);
    printf("\nexec: program returned\n");
}

static void cmd_cat(char *args) {
    FIL file;
    FRESULT res;
    char buffer[128];
    UINT bytes_read;

    if (args == NULL || *args == '\0') {
        printf("Usage: cat <filename>\n");
        return;
    }

    res = f_open(&file, args, FA_READ);
    if (res != FR_OK) {
        printf("cat: cannot open %s (error %d)\n", args, res);
        return;
    }

    while (1) {
        res = f_read(&file, buffer, sizeof(buffer) - 1, &bytes_read);
        if (res != FR_OK) {
            printf("cat: read error %d\n", res);
            break;
        }

        if (bytes_read == 0)
            break;

        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }

    f_close(&file);
}

static void cmd_ls(char *args) {
    DIR dir;
    FILINFO info;
    FRESULT res;

    const char *path = (args && *args) ? args : ".";

    res = f_opendir(&dir, path);
    if (res != FR_OK) {
        printf("ls: cannot open %s (error %d)\n", path, res);
        return;
    }

    while (1) {
        res = f_readdir(&dir, &info);
        if (res != FR_OK || info.fname[0] == '\0')
            break;

        if (info.fattrib & AM_DIR)
            printf("<DIR>  %s\n", info.fname);
        else
            printf("%u  %s\n", (unsigned int)info.fsize, info.fname);
    }

    f_closedir(&dir);
}

static void cmd_about(char *args) {
    (void)args;
    printf("DOS-32 is a 32-Bit OS Created by Darii, Started on 25th of july, 2026, Shortly after DOS-16\n");
}

static void cmd_clear(char *args) {
    (void)args;
    terminal_initialize();
}

static void cmd_echo(char *args) {
    printf("%s\n", args ? args : "");
}

static const char *command_names[] = { "help", "about", "cd", "clear", "echo", "ls", "pwd", "cat", "write", "exec", "mkdir", "rm", "mv" };
static void (*const command_funcs[])(char *) = { cmd_help, cmd_about, cmd_cd, cmd_clear, cmd_echo, cmd_ls, cmd_pwd, cmd_cat, cmd_write, cmd_exec, cmd_mkdir, cmd_rm, cmd_mv };
#define NUM_COMMANDS (sizeof(command_names) / sizeof(command_names[0]))

void shell_run(void) {
    
    char line[128];

    while (1) {
        printf("$> ");
        readline(line, sizeof(line));

        char *input = line;
        while (*input == ' ' || *input == '\t')
            input++;

        if (*input == '\0')
            continue;

        char *args = strchr(input, ' ');
        if (args != NULL) {
            *args++ = '\0';
            while (*args == ' ')
                args++;
        }

        int found = 0;
        for (size_t i = 0; i < NUM_COMMANDS; i++) {
            if (strcmp(input, command_names[i]) == 0) {
                command_funcs[i](args);
                found = 1;
                break;
            }
        }

        if (!found && !try_user_program(input, args))
            printf("Unknown command: %s\n", input);
    }
}
