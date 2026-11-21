#include <stdio.h>
#include <kernel/tty.h>
#include "serial.h"
#include "keyboard.h"
#include "../boot_info.h"
#include "pmm.h"
#include "heap.h"
#include "ff.h"
#include "shell.h"
#include "idt.h"
#include "arch/i386/pic.h"
#include "timer.h"
#include "pci.h"
#include "usb_msc.h"

static void report_usb_controller(const struct pci_device *dev, void *context)
{
    (void)context;
    if (dev->class_code != PCI_CLASS_SERIAL || dev->subclass != PCI_SUBCLASS_USB)
        return;

    if (dev->prog_if == PCI_PROGIF_EHCI)
        printf("PCI: EHCI USB 2.0 controller at %u:%u.%u\n",
               dev->bus, dev->slot, dev->function);
    else if (dev->prog_if == PCI_PROGIF_XHCI)
        printf("PCI: xHCI USB controller at %u:%u.%u\n",
               dev->bus, dev->slot, dev->function);
}

void kernel_main(struct boot_info* bi)
{
    terminal_initialize();
    idt_install();
    pic_init();
    keyboard_init();
    timer_init();
    __asm__ volatile ("sti");
    printf("PIC INITIALIZED\n");
    printf("IDT INITIALIZED\n");

    if (!bi || bi->magic != BOOT_MAGIC)
    {
        printf("Invalid boot information!\n");
        while (1);
    }

    if (!(bi->flags & BIF_E820) || !bi->mmap_count)
    {
        printf("E820 memory map unavailable!\n");
        while (1);
    }

    pmm_init(bi->mmap_addr, bi->mmap_count);
    heap_init();
    pci_scan(report_usb_controller, 0);
    int usb_result = usb_msc_init();
    if (usb_result == 0)
        printf("USB EHCI mass storage ready (volume 2:)\n");
    else
        printf("USB EHCI mass storage unavailable (code %d)\n", usb_result);

    printf("DOS-32 Memory Manager\n");

    printf("Free pages: %u\n", pmm_get_free_pages());

    void* page = pmm_alloc_page();
    void* memory = kmalloc(128);

    printf("Page allocation: %s\n", page ? "SUCCESS" : "FAILED");
    printf("Heap allocation: %s\n", memory ? "SUCCESS" : "FAILED");
    printf("Hello World!\n");
    printf("Initializing Terminal...\n");

    FATFS fs;
    FATFS usb_fs;
    FIL file;
    FRESULT res;

    res = f_mount(&fs, "0:", 1);
    printf("\nf_mount result: %d\n", res);

    if (usb_result == 0) {
        FRESULT usb_mount = f_mount(&usb_fs, "2:", 1);
        printf("USB mount at MNT: %s (result %d)\n",
               usb_mount == FR_OK ? "ready" : "failed", usb_mount);
        if (usb_mount == FR_OK)
            f_mkdir("0:/MNT");
    }

    if (res == FR_OK)
    {
        res = f_open(&file, "0:/HELLO.TXT", FA_WRITE | FA_CREATE_ALWAYS);
        if (res == FR_OK)
        {
            UINT written = 0;
            const char* msg = "Hello from DOS-32 FAT32!\n";
            f_write(&file, msg, 25, &written);
            f_close(&file);
            printf("Wrote %u bytes to 0:/HELLO.TXT\n", written);
        }

        res = f_open(&file, "0:/HELLO.TXT", FA_READ);
        if (res == FR_OK)
        {
            char buf[64] = {0};
            UINT read_bytes = 0;
            f_read(&file, buf, sizeof(buf) - 1, &read_bytes);
            f_close(&file);
            printf("Read back (%u bytes): %s", read_bytes, buf);
        }
    }
    printf("Say 'help' to view current commands\n");
    shell_run();
}
