/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * In-memory RAM disk backing the REAL FAT driver (kernel/fat.c) in
 * the unit-test runner. Provides host implementations of the IDE
 * block API (ide_read_sector / ide_write_sector) plus helpers that
 * format the RAM disk as a valid FAT16 volume in pure C and inject
 * files into its root directory - no external tools involved.
 */

#ifndef FAT_FAKE_DISK_H
#define FAT_FAKE_DISK_H

#include <stdint.h>

#define FATDISK_SECTOR_SIZE   512u
#define FATDISK_TOTAL_SECTORS 8192u          /* 4 MiB RAM disk */
#define FATDISK_PART_LBA      2048u          /* partition start in MBR mode */

/* Volume geometry shared by both format modes (FAT16, 1 sector/cluster) */
#define FATDISK_RESERVED_SECTORS 1u
#define FATDISK_FAT_COUNT        2u
#define FATDISK_SECTORS_PER_FAT  32u
#define FATDISK_ROOT_ENTRIES     512u        /* => 32 root dir sectors */
#define FATDISK_ROOT_START       65u         /* relative: 1 + 2*32 */
#define FATDISK_DATA_START       97u         /* relative: 65 + 512*32/512 */

/* Zero the whole RAM disk (an unformatted, signature-less image). */
void fatdisk_reset(void);

/*
 * Format the RAM disk as a raw FAT16 volume starting at LBA 0
 * (boot sector with BPB + "FAT16" tag, two FATs, empty root
 * directory, zeroed data area). Total sectors: 8192 => 8095 clusters,
 * which fat.c classifies as FAT16. Returns 0.
 */
int fatdisk_format(void);

/*
 * Format the RAM disk with an MBR containing one FAT16 partition
 * (type 0x06) at FATDISK_PART_LBA, and a FAT16 volume inside it
 * (6144 sectors => 6047 clusters, still FAT16). Returns 0.
 */
int fatdisk_format_mbr(void);

/*
 * Inject a file into the root directory of the formatted volume.
 * name is a normal "NAME.EXT" style filename (converted to 8.3
 * internally, independent of fat.c's own converter). Allocates a
 * cluster chain, writes it to both FAT copies and copies the data
 * into the data area. Returns 0 on success, -1 on error.
 */
int fatdisk_add_file(const char *name, const void *data, uint32_t size);

/*
 * Same as fatdisk_add_file() but with an explicit attribute byte, e.g.
 * FAT_ATTR_READ_ONLY, FAT_ATTR_HIDDEN or FAT_ATTR_SYSTEM combinations.
 * fatdisk_add_file() is this with FAT_ATTR_ARCHIVE.
 */
int fatdisk_add_file_attr(const char *name, const void *data, uint32_t size,
                          uint8_t attributes);

/*
 * Add a synthetic long-filename slot (attributes 0x0F) to the root
 * directory. Only the attribute byte matters to the driver's LFN skip
 * logic; the rest of the entry mimics a last-in-sequence LFN record.
 */
int fatdisk_add_lfn_entry(void);

/* Add a volume-label entry (attribute 0x08) to the root directory. */
int fatdisk_add_volume_label(const char *label);

/* Add a deleted (0xE5) entry to the root directory. */
int fatdisk_add_deleted_entry(void);

#endif /* FAT_FAKE_DISK_H */
