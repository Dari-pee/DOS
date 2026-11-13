#ifndef BOOT_INFO_H
#define BOOT_INFO_H
#include <stdint.h>

#define BOOT_MAGIC 0x544F4F42u
#define BIF_A20       (1u << 0)
#define BIF_E820      (1u << 1)         /* map is from BIOS E820 (else synthesized) */
#define BIF_CPUID     (1u << 2)
#define BIF_LONGMODE  (1u << 3)         /* CPU supports 64-bit mode */
#define BIF_RAMTEST   (1u << 4) 

/* e820_entry.type */
#define E820_USABLE   1
#define E820_RESERVED 2
#define E820_ACPI_RECLAIM 3
#define E820_ACPI_NVS 4
#define E820_BAD      5

/* ramtest_status */
#define RAMTEST_NOT_RUN 0
#define RAMTEST_PASSED  1
#define RAMTEST_FAILED  2

struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_attr;
} __attribute__((packed));

struct boot_info {
    uint32_t magic;                 /* BOOT_MAGIC */
    uint32_t version;               /* 1 */
    uint32_t boot_drive;            /* BIOS drive number, e.g. 0x80 */
    uint32_t flags;
    uint32_t mmap_count;            /* number of e820_entry */
    uint32_t mmap_addr;
    uint32_t low_mem_kb;
    uint32_t ext_1m_16m_kb;
    uint32_t ext_above_16m_64k;
    uint32_t total_usable_kb;
    uint32_t ram_top;
    uint32_t kernel_addr;
    uint32_t kernel_sectors;
    uint32_t ramtest_status;
    uint32_t ramtest_kb;
    uint32_t ramtest_fail_addr;
    uint32_t ramtest_fail_expected;
    uint32_t ramtest_fail_actual;
    uint32_t cpuid_max_leaf;
    char     cpu_vendor[12];
    uint32_t cpu_features_edx;      /* CPUID.1:EDX */
    uint32_t cpu_features_ecx;      /* CPUID.1:ECX */
    uint32_t cpu_ext_features_edx;  /* CPUID.80000001h:EDX */
} __attribute__((packed));

#endif
