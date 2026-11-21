#!/bin/bash
set -e
export PATH="$PATH:/usr/local/i386elfgcc/bin"

if command -v i386-elf-gcc >/dev/null; then CC=i386-elf-gcc; LD=i386-elf-ld
else CC="gcc -m32"; LD="ld -m elf_i386"; EXTRA="-nostdinc -isystem $(gcc -print-file-name=include) -isystem Binaries/stub"; fi

CFLAGS="-ffreestanding -mgeneral-regs-only -fno-pie -fno-stack-protector -fno-builtin -O2 -Wall \
 -Ilibc/include -Ikernel/include -Ikernel/arch/i386 -D__is_libk $EXTRA"

rm -rf Binaries
mkdir -p Binaries/stub
: > Binaries/stub/limits.h
mkdir -p BIN/output
rm -f BIN/output/*.o BIN/output/*.BIN BIN/output/*.ELF
for psrc in BIN/*.c; do
    [ -e "$psrc" ] || continue
    pname=$(basename "$psrc" .c | tr 'a-z' 'A-Z')
    if [ ${#pname} -gt 8 ]; then
        echo "Skipping $psrc: use a name of 8 characters or less"
        continue
    fi
    $CC -ffreestanding -mgeneral-regs-only -fno-pie -fno-stack-protector -fno-builtin -O2 -Wall \
        -Ikernel/arch/i386 -c "$psrc" -o "BIN/output/$pname.o"
    $LD -T BIN/program.ld --oformat binary -o "BIN/output/$pname.BIN" "BIN/output/$pname.o"
    echo "Program: $pname.BIN"
    $LD -T BIN/elfprogram.ld -o "BIN/output/$pname.ELF" "BIN/output/$pname.o"
    echo "Program: $pname.ELF"
done

nasm -f elf32 kernel_entry.asm -o Binaries/kernel_entry.o
OBJS="Binaries/kernel_entry.o"
for src in kernel/kernel.c kernel/arch/i386/tty.c kernel/arch/i386/idt.c kernel/arch/i386/pic.c kernel/arch/i386/timer.c kernel/arch/i386/pci.c kernel/arch/i386/usb_msc.c kernel/arch/i386/shell.c kernel/arch/i386/diskio.c kernel/arch/i386/ata.c kernel/arch/i386/pmm.c kernel/arch/i386/ff.c kernel/arch/i386/heap.c kernel/arch/i386/keyboard.c kernel/include/kernel/serial.c io.c libc/string/*.c libc/stdlib/*.c libc/stdio/printf.c; do
    obj="Binaries/$(echo "$src" | tr '/' '_').o"
    $CC $CFLAGS -c "$src" -o "$obj"
    OBJS="$OBJS $obj"
done
$LD -T linker.ld --oformat binary -o Binaries/kernel.bin $OBJS

KSEC=$(( ( $(stat -c%s Binaries/kernel.bin) + 511 ) / 512 ))
nasm -f bin boot.asm -o Binaries/boot.bin
nasm -f bin -DKERNEL_SECTORS=$KSEC stage2.asm -o Binaries/stage2.bin
cat Binaries/boot.bin Binaries/stage2.bin Binaries/kernel.bin > Binaries/OS.bin
truncate -s 1474560 Binaries/OS.bin
echo "Kernel: $KSEC sectors"

[ "$1" = "--no-run" ] && exit 0

if [ ! -f fat32.img ]; then
    dd if=/dev/zero of=fat32.img bs=1M count=64
    mkfs.fat -F 32 fat32.img
fi

# A separate raw image is exposed to QEMU as an EHCI USB flash drive.
if [ ! -f usb.img ]; then
    dd if=/dev/zero of=usb.img bs=1M count=64 status=none
    mkfs.fat -F 32 -n USBSTICK usb.img >/dev/null
fi

if command -v mcopy >/dev/null; then
    if ! timeout -k 1s 10s mdir -i fat32.img ::USER >/dev/null 2>&1; then
        if ! MTOOLS_LOCK_TIMEOUT=2 timeout -k 1s 10s mmd -i fat32.img ::USER; then
            echo "Could not create USER on fat32.img. Close programs using the image and try again." >&2
            exit 1
        fi
    fi
    for program in BIN/output/*.BIN BIN/output/*.ELF; do
        [ -e "$program" ] || continue
        if ! MTOOLS_LOCK_TIMEOUT=2 timeout -k 1s 15s mcopy -D o -i fat32.img "$program" ::USER/; then
            echo "Could not copy $program to fat32.img. Close programs using the image and try again." >&2
            exit 1
        fi
    done
else
    echo "mtools not found: run 'sudo apt install mtools' so programs are copied into the disk image"
fi

qemu-system-i386 -boot order=a -drive format=raw,file=Binaries/OS.bin,index=0,if=floppy -drive format=raw,file=fat32.img,index=0,media=disk -drive if=none,id=usbdrive,format=raw,file=usb.img -device usb-ehci,id=ehci -device usb-storage,bus=ehci.0,drive=usbdrive -serial stdio -m 128M
