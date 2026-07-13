/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * In-memory RAM disk + FAT16 formatter backing the REAL kernel/fat.c
 * in the unit-test runner.
 *
 * kernel/fat.c reaches hardware through exactly three externals:
 *   - ide_read_sector / ide_write_sector (include/drivers/ide.h):
 *     implemented here against a static 4 MiB buffer, mimicking the
 *     real driver's convention (returns the sector count 1 on
 *     success, negative on error).
 *   - serial_printf (include/kernel/serial.h): swallowed to keep the
 *     test output clean.
 *
 * The formatter lays down a bit-exact FAT16 volume in pure C:
 * boot sector with BPB + 0xAA55 signature + "FAT16" tag (which
 * fat_find_partition uses to recognise raw volumes), two FAT copies,
 * a 512-entry root directory and a zeroed data area. A second mode
 * wraps the same volume in an MBR partition (type 0x06 at LBA 2048)
 * to exercise fat_init()'s partition scan.
 *
 * Geometry (1 sector per cluster, 512-byte sectors):
 *   raw:  8192 total sectors -> 8095 clusters  (FAT16 range)
 *   MBR:  6144 sector partition -> 6047 clusters (FAT16 range)
 *   FAT:  32 sectors per copy = 8192 entries, enough for either mode
 */

#include <stdint.h>
#include <string.h>

#include "fat_fake_disk.h"
#include "../../include/kernel/fat.h"     /* fat16_ebpb_t, fat_dir_entry_t */
#include "../../include/kernel/serial.h"  /* serial_printf prototype */
#include "../../include/drivers/ide.h"    /* ide_*_sector prototypes */

static uint8_t  fatdisk_image[FATDISK_TOTAL_SECTORS * FATDISK_SECTOR_SIZE];
static uint32_t part_base = 0;         /* LBA where the FAT volume starts   */
static uint32_t next_free_cluster = 0; /* next allocatable cluster, 0 = raw */
static uint32_t volume_clusters = 0;   /* clusters in the data area         */

static uint8_t *sector_ptr(uint32_t lba) {
    return &fatdisk_image[(size_t)lba * FATDISK_SECTOR_SIZE];
}

/* Write a FAT16 entry into BOTH FAT copies. */
static void fat16_set(uint32_t cluster, uint16_t value) {
    for (uint32_t f = 0; f < FATDISK_FAT_COUNT; f++) {
        uint32_t lba = part_base + FATDISK_RESERVED_SECTORS +
                       f * FATDISK_SECTORS_PER_FAT + (cluster * 2) / FATDISK_SECTOR_SIZE;
        uint8_t *p = sector_ptr(lba) + (cluster * 2) % FATDISK_SECTOR_SIZE;
        p[0] = (uint8_t)(value & 0xFF);
        p[1] = (uint8_t)(value >> 8);
    }
}

/* Convert "NAME.EXT" (optionally with leading '/') to padded 8.3. */
static void name_to_83(const char *name, uint8_t out[11]) {
    memset(out, ' ', 11);
    if (name[0] == '/') {
        name++;
    }

    int i = 0;
    for (; name[i] && name[i] != '.' && i < 8; i++) {
        char c = name[i];
        out[i] = (uint8_t)((c >= 'a' && c <= 'z') ? c - 32 : c);
    }

    const char *dot = strchr(name, '.');
    if (dot) {
        for (int j = 0; dot[1 + j] && j < 3; j++) {
            char c = dot[1 + j];
            out[8 + j] = (uint8_t)((c >= 'a' && c <= 'z') ? c - 32 : c);
        }
    }
}

/* First unused (name[0] == 0x00) root directory slot, or NULL. */
static fat_dir_entry_t *find_free_root_slot(void) {
    uint32_t root_lba = part_base + FATDISK_ROOT_START;
    uint32_t root_sectors = FATDISK_ROOT_ENTRIES * 32 / FATDISK_SECTOR_SIZE;

    for (uint32_t s = 0; s < root_sectors; s++) {
        fat_dir_entry_t *entries = (fat_dir_entry_t *)sector_ptr(root_lba + s);
        for (int e = 0; e < 16; e++) {
            if (entries[e].name[0] == 0x00) {
                return &entries[e];
            }
        }
    }
    return 0;
}

static void format_volume(uint32_t base, uint16_t total_sectors) {
    part_base = base;
    next_free_cluster = 2;
    volume_clusters = (uint32_t)total_sectors - FATDISK_DATA_START;

    fat16_ebpb_t *bs = (fat16_ebpb_t *)sector_ptr(base);
    bs->bpb.jump[0] = 0xEB;
    bs->bpb.jump[1] = 0x3C;
    bs->bpb.jump[2] = 0x90;
    memcpy(bs->bpb.oem_name, "TOCINTST", 8);
    bs->bpb.bytes_per_sector    = FATDISK_SECTOR_SIZE;
    bs->bpb.sectors_per_cluster = 1;
    bs->bpb.reserved_sectors    = FATDISK_RESERVED_SECTORS;
    bs->bpb.fat_count           = FATDISK_FAT_COUNT;
    bs->bpb.root_entry_count    = FATDISK_ROOT_ENTRIES;
    bs->bpb.total_sectors_16    = total_sectors;
    bs->bpb.media_type          = 0xF8;
    bs->bpb.sectors_per_fat_16  = FATDISK_SECTORS_PER_FAT;
    bs->bpb.sectors_per_track   = 63;
    bs->bpb.head_count          = 16;
    bs->bpb.hidden_sectors      = base;
    bs->bpb.total_sectors_32    = 0;
    bs->drive_number   = 0x80;
    bs->reserved1      = 0;
    bs->boot_signature = 0x29;
    bs->volume_id      = 0x20260713;
    memcpy(bs->volume_label, "TOCINOS    ", 11);
    memcpy(bs->fs_type, "FAT16   ", 8);  /* "FAT" tag at offset 0x36 */

    uint8_t *raw = sector_ptr(base);
    raw[510] = 0x55;
    raw[511] = 0xAA;

    /* FAT[0] = media descriptor, FAT[1] = end-of-chain */
    fat16_set(0, 0xFFF8);
    fat16_set(1, 0xFFFF);
}

void fatdisk_reset(void) {
    memset(fatdisk_image, 0, sizeof(fatdisk_image));
    part_base = 0;
    next_free_cluster = 0;
    volume_clusters = 0;
}

int fatdisk_format(void) {
    fatdisk_reset();
    format_volume(0, (uint16_t)FATDISK_TOTAL_SECTORS);
    return 0;
}

int fatdisk_format_mbr(void) {
    fatdisk_reset();
    format_volume(FATDISK_PART_LBA,
                  (uint16_t)(FATDISK_TOTAL_SECTORS - FATDISK_PART_LBA));

    /* MBR: one active FAT16 (type 0x06) partition at FATDISK_PART_LBA */
    uint8_t *mbr = sector_ptr(0);
    uint8_t *p = mbr + 446;               /* first partition entry */
    uint32_t lba = FATDISK_PART_LBA;
    uint32_t count = FATDISK_TOTAL_SECTORS - FATDISK_PART_LBA;
    p[0] = 0x80;                          /* bootable */
    p[4] = 0x06;                          /* FAT16 */
    p[8]  = (uint8_t)(lba & 0xFF);
    p[9]  = (uint8_t)((lba >> 8) & 0xFF);
    p[10] = (uint8_t)((lba >> 16) & 0xFF);
    p[11] = (uint8_t)((lba >> 24) & 0xFF);
    p[12] = (uint8_t)(count & 0xFF);
    p[13] = (uint8_t)((count >> 8) & 0xFF);
    p[14] = (uint8_t)((count >> 16) & 0xFF);
    p[15] = (uint8_t)((count >> 24) & 0xFF);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    return 0;
}

int fatdisk_add_file(const char *name, const void *data, uint32_t size) {
    if (!next_free_cluster || !name || (!data && size)) {
        return -1;
    }

    uint32_t clusters = (size + FATDISK_SECTOR_SIZE - 1) / FATDISK_SECTOR_SIZE;
    if (next_free_cluster + clusters - 2 > volume_clusters) {
        return -1;  /* volume full */
    }

    fat_dir_entry_t *entry = find_free_root_slot();
    if (!entry) {
        return -1;
    }

    uint32_t first = clusters ? next_free_cluster : 0;
    for (uint32_t i = 0; i < clusters; i++) {
        uint32_t c = next_free_cluster + i;
        fat16_set(c, (i == clusters - 1) ? 0xFFFF : (uint16_t)(c + 1));

        uint32_t remaining = size - i * FATDISK_SECTOR_SIZE;
        uint32_t chunk = remaining < FATDISK_SECTOR_SIZE ? remaining
                                                         : FATDISK_SECTOR_SIZE;
        memcpy(sector_ptr(part_base + FATDISK_DATA_START + (c - 2)),
               (const uint8_t *)data + i * FATDISK_SECTOR_SIZE, chunk);
    }
    next_free_cluster += clusters;

    name_to_83(name, entry->name);
    entry->attributes = FAT_ATTR_ARCHIVE;
    entry->first_cluster_high = 0;
    entry->first_cluster_low = (uint16_t)first;
    entry->file_size = size;
    return 0;
}

int fatdisk_add_volume_label(const char *label) {
    if (!next_free_cluster || !label) {
        return -1;
    }
    fat_dir_entry_t *entry = find_free_root_slot();
    if (!entry) {
        return -1;
    }

    memset(entry->name, ' ', 11);
    for (int i = 0; label[i] && i < 11; i++) {
        entry->name[i] = (uint8_t)label[i];
    }
    entry->attributes = FAT_ATTR_VOLUME_ID;
    entry->first_cluster_high = 0;
    entry->first_cluster_low = 0;
    entry->file_size = 0;
    return 0;
}

int fatdisk_add_deleted_entry(void) {
    if (!next_free_cluster) {
        return -1;
    }
    fat_dir_entry_t *entry = find_free_root_slot();
    if (!entry) {
        return -1;
    }

    memcpy(entry->name, "OLDFILE TXT", 11);
    entry->name[0] = 0xE5;  /* deleted marker */
    entry->attributes = FAT_ATTR_ARCHIVE;
    entry->file_size = 123;
    return 0;
}

/* ---------------- IDE block API (see include/drivers/ide.h) ------------- */

int ide_read_sector(uint8_t drive, uint32_t lba, void *buffer) {
    if (drive != 0 || !buffer || lba >= FATDISK_TOTAL_SECTORS) {
        return -1;
    }
    memcpy(buffer, sector_ptr(lba), FATDISK_SECTOR_SIZE);
    return 1;  /* sectors transferred, like the real driver */
}

int ide_write_sector(uint8_t drive, uint32_t lba, const void *buffer) {
    if (drive != 0 || !buffer || lba >= FATDISK_TOTAL_SECTORS) {
        return -1;
    }
    memcpy(sector_ptr(lba), buffer, FATDISK_SECTOR_SIZE);
    return 1;
}

/* fat.c logs through serial_printf; swallow it on the host. */
void serial_printf(const char *fmt, ...) {
    (void)fmt;
}
