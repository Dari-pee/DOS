#!/bin/bash
set -e

export PATH="$PATH:/usr/local/i386elfgcc/bin"

mkdir -p build

echo "[1/9] Assembling first stage..."
nasm boot.asm -f bin -o build/boot.bin

echo "[2/9] Assembling second stage..."
nasm bootwo.asm -f bin -o build/bootwo.bin

echo "[3/9] Assembling kernel entry..."
nasm kernel_entry.asm -f elf -o build/kernel_entry.o

echo "[4/9] Assembling desktop..."
nasm desktop.asm -f elf -o build/desktop.o

echo "[4b/9] Assembling real-mode BIOS thunk..."
nasm realmode.asm -f elf -o build/realmode.o

echo "[5/9] Compiling kernel..."
i386-elf-gcc \
    -m32 \
    -ffreestanding \
    -fno-pie \
    -fno-pic \
    -fno-stack-protector \
    -c kernel.c \
    -o build/kernel.o

i386-elf-gcc \
    -m32 \
    -ffreestanding \
    -fno-pie \
    -fno-pic \
    -fno-stack-protector \
    -mgeneral-regs-only \
    -c idt.c \
    -o build/idt.o

i386-elf-gcc \
    -m32 \
    -ffreestanding \
    -fno-pie \
    -fno-pic \
    -fno-stack-protector \
    -c vga.c \
    -o build/vga.o

i386-elf-gcc \
    -m32 \
    -ffreestanding \
    -fno-pie \
    -fno-pic \
    -fno-stack-protector \
    -c mouse.c \
    -o build/mouse.o

echo "[6/9] Linking kernel..."
i386-elf-ld \
    -m elf_i386 \
    -Ttext 0x1000 \
    --entry=_start \
    --oformat binary \
    -o build/full_kernel.bin \
    build/kernel_entry.o \
    build/kernel.o \
    build/idt.o \
    build/vga.o \
    build/mouse.o \
    build/desktop.o \
    build/realmode.o

echo "[7/9] Checking bootloader..."

BOOT_SIZE=$(stat -c%s build/boot.bin)
STAGE2_SIZE=$(stat -c%s build/bootwo.bin)

if [ "$BOOT_SIZE" -ne 512 ]; then
    echo "ERROR: boot.bin is $BOOT_SIZE bytes"
    exit 1
fi

if [ "$STAGE2_SIZE" -gt 2048 ]; then
    echo "ERROR: bootwo.bin is $STAGE2_SIZE bytes"
    echo "Second stage must fit in sectors 2-5."
    exit 1
fi

echo "boot.bin   = $BOOT_SIZE bytes"
echo "bootwo.bin = $STAGE2_SIZE bytes"

echo "[8/9] Creating FAT12 filesystem..."

rm -f build/OS.bin

dd if=/dev/zero \
    of=build/OS.bin \
    bs=512 \
    count=2880 \
    status=none

mkfs.fat \
    -F 12 \
    -R 5 \
    -S 512 \
    -s 1 \
    -f 2 \
    -r 224 \
    -n "DARIOS" \
    build/OS.bin

echo "[9/9] Installing bootloader and files..."

dd if=build/boot.bin \
    of=build/OS.bin \
    bs=512 \
    count=1 \
    conv=notrunc \
    status=none

dd if=build/bootwo.bin \
    of=build/OS.bin \
    bs=512 \
    seek=1 \
    conv=notrunc \
    status=none

mcopy -i build/OS.bin \
    build/full_kernel.bin \
    ::KERNEL.BIN

# Base directory tree for DOS-32.  The kernel can already list these root
# entries; navigating into them will be added with the runtime FAT12 driver.
mmd -i build/OS.bin ::APPS
mmd -i build/OS.bin ::DOCS
mmd -i build/OS.bin ::SYSTEM
mmd -i build/OS.bin ::HOME

echo
echo "=============================="
echo " DOS-32 FAT12 IMAGE READY"
echo "=============================="

echo
echo "Files on FAT12 image:"
mdir -i build/OS.bin ::

echo
echo "Image:"
ls -lh build/OS.bin

echo
echo "Starting QEMU..."

qemu-system-i386 \
    -drive format=raw,file=build/OS.bin,index=0,if=floppy \
    -m 128M \
    -boot a
