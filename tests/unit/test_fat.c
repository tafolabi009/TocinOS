/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 *
 * Unit tests for the REAL FAT driver (kernel/fat.c), compiled
 * directly into the test runner - no mocks.
 *
 * The block layer underneath (ide_read_sector / ide_write_sector) is
 * an in-memory RAM disk (tests/framework/fat_fake_disk.c) formatted
 * as a genuine FAT16 volume in pure C, either as a raw image or
 * behind an MBR partition table, with test files injected into the
 * root directory.
 *
 * Write support: fat_write() is not implemented in kernel/fat.c (it
 * returns -1 by design), so the file-level tests are read-only and
 * one test pins down that contract. The sector-level write path
 * (fat_write_sector) IS implemented and is round-tripped here.
 *
 * State handling: fat.c keeps the mount in static globals, so every
 * test rebuilds the RAM disk and remounts through mount_fresh_volume()
 * for a deterministic starting state.
 */

#include "../framework/unittest.h"
#include "../framework/fat_fake_disk.h"
#include "../../include/kernel/fat.h"
#include <string.h>

/* Known file contents injected into every freshly formatted volume. */
static const char test_txt[] = "Hello from the TocinOS FAT16 test volume!\n";
#define TEST_TXT_LEN ((uint32_t)sizeof(test_txt) - 1u)

#define HELLO_LEN 2000u                /* spans 4 clusters at 512 B/cluster */
static uint8_t hello_bytes[HELLO_LEN];

static uint8_t hello_byte_at(uint32_t i) {
    static const uint8_t magic[4] = {0x7F, 'E', 'L', 'F'};
    return (i < 4) ? magic[i] : (uint8_t)(i * 7 + 3);
}

/* Root directory: volume label + deleted entry + TEST.TXT + HELLO.ELF */
static void populate_root(void) {
    for (uint32_t i = 0; i < HELLO_LEN; i++) {
        hello_bytes[i] = hello_byte_at(i);
    }
    fatdisk_add_volume_label("TOCINOS");
    fatdisk_add_deleted_entry();
    fatdisk_add_file("TEST.TXT", test_txt, TEST_TXT_LEN);
    fatdisk_add_file("HELLO.ELF", hello_bytes, HELLO_LEN);
}

/* Rebuild the raw FAT16 image and mount it. Returns fat_mount result. */
static int mount_fresh_volume(void) {
    fatdisk_format();
    populate_root();
    fat_unmount();                     /* drop any mount from a prior test */
    fat_set_partition_offset(0);
    return fat_mount(0);
}

TEST_SUITE(fat_tests)

    // Mounting parses the BPB into the expected FAT16 layout
    TEST_CASE(mount_geometry)
        ASSERT_EQ(mount_fresh_volume(), 0, "Freshly formatted volume mounts");
        ASSERT_EQ(fat_is_mounted(), 1, "Driver reports mounted");

        fat_info_t *info = fat_get_info();
        ASSERT_TRUE(info != 0, "Filesystem info is exposed");
        ASSERT_EQ(info->type, FAT_TYPE_FAT16, "8095 clusters classify as FAT16");
        ASSERT_EQ(info->bytes_per_sector, 512, "Sector size from BPB");
        ASSERT_EQ(info->sectors_per_cluster, 1, "Cluster size from BPB");
        ASSERT_EQ(info->fat_count, 2, "Two FAT copies");
        ASSERT_EQ(info->reserved_sectors, 1, "One reserved sector");
        ASSERT_EQ(info->sectors_per_fat, 32u, "FAT size from BPB");
        ASSERT_EQ(info->root_entry_count, 512, "Root directory entries");
        ASSERT_EQ(info->fat_start, 1u, "FAT follows the boot sector");
        ASSERT_EQ(info->root_start, 65u, "Root dir follows both FATs");
        ASSERT_EQ(info->data_start, 97u, "Data area follows the root dir");
        ASSERT_EQ(info->cluster_count, 8095u, "Data sectors / cluster size");

        ASSERT_EQ(fat_cluster_to_sector(2), 97u,
                  "First data cluster maps to the first data sector");
    END_TEST_CASE()

    // Garbage disks and missing drives are rejected without mounting
    TEST_CASE(mount_rejects_garbage)
        fat_unmount();
        fatdisk_reset();               /* all zeros: no 0xAA55 signature */
        fat_set_partition_offset(0);

        ASSERT_EQ(fat_mount(0), -1, "Unformatted disk does not mount");
        ASSERT_EQ(fat_is_mounted(), 0, "Driver reports unmounted");
        ASSERT_TRUE(fat_get_info() == 0, "No filesystem info when unmounted");

        fat_file_t f;
        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("TEST.TXT", &f), -1, "Open fails when unmounted");

        ASSERT_NE(fat_init(3), 0, "fat_init fails on a drive that is absent");
    END_TEST_CASE()

    // fat_init auto-detects a raw FAT image (no partition table)
    TEST_CASE(init_autodetect_raw_image)
        fatdisk_format();
        populate_root();
        fat_unmount();

        ASSERT_EQ(fat_init(0), 0, "Raw image detected via FAT16 tag at 0x36");
        ASSERT_EQ(fat_is_mounted(), 1, "Volume mounted");

        fat_file_t f;
        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("TEST.TXT", &f), 0, "File reachable at offset 0");
        fat_close(&f);
    END_TEST_CASE()

    // fat_init finds the FAT16 partition in an MBR and applies its offset
    TEST_CASE(init_autodetect_mbr_partition)
        fatdisk_format_mbr();
        populate_root();
        fat_unmount();

        ASSERT_EQ(fat_init(0), 0, "MBR partition (type 0x06) detected");
        ASSERT_EQ(fat_is_mounted(), 1, "Partition volume mounted");

        fat_info_t *info = fat_get_info();
        ASSERT_TRUE(info != 0, "Filesystem info is exposed");
        ASSERT_EQ(info->type, FAT_TYPE_FAT16, "6047 clusters classify as FAT16");
        ASSERT_EQ(info->cluster_count, 6047u, "Partition-sized cluster count");

        // Every sector access must be shifted by the partition offset
        fat_file_t f;
        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("TEST.TXT", &f), 0, "File found inside the partition");
        char buf[64];
        int n = fat_read(&f, buf, sizeof(buf));
        ASSERT_EQ(n, (int)TEST_TXT_LEN, "Full file read through the offset");
        ASSERT_EQ(memcmp(buf, test_txt, TEST_TXT_LEN), 0,
                  "Content read from the partitioned volume matches");
        fat_close(&f);
    END_TEST_CASE()

    // Open + read returns the exact injected bytes
    TEST_CASE(open_and_read_exact)
        ASSERT_EQ(mount_fresh_volume(), 0, "Volume mounts");

        fat_file_t f;
        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("TEST.TXT", &f), 0, "File opens");
        ASSERT_EQ(f.size, TEST_TXT_LEN, "Directory entry size is reported");
        ASSERT_EQ(f.is_open, 1, "Handle marked open");

        char buf[128];
        memset(buf, 0xAA, sizeof(buf));
        int n = fat_read(&f, buf, sizeof(buf));
        ASSERT_EQ(n, (int)TEST_TXT_LEN, "Read clamps to the file size");
        ASSERT_EQ(memcmp(buf, test_txt, TEST_TXT_LEN), 0,
                  "Content matches byte-for-byte");
        ASSERT_EQ(fat_read(&f, buf, sizeof(buf)), 0, "Second read hits EOF");

        // 8.3 lookup is case-insensitive and tolerates a leading slash
        fat_file_t g;
        memset(&g, 0, sizeof(g));
        ASSERT_EQ(fat_open("/test.txt", &g), 0, "Lowercase + slash resolves");
        ASSERT_EQ(g.size, TEST_TXT_LEN, "Same file found");
        fat_close(&g);

        ASSERT_EQ(fat_close(&f), 0, "Close succeeds");
        ASSERT_EQ(f.is_open, 0, "Handle marked closed");
        ASSERT_EQ(fat_read(&f, buf, 1), -1, "Read on closed handle fails");
        ASSERT_EQ(fat_close(&f), -1, "Double close fails");
    END_TEST_CASE()

    // Lookups that cannot succeed fail cleanly
    TEST_CASE(missing_file_fails_cleanly)
        ASSERT_EQ(mount_fresh_volume(), 0, "Volume mounts");

        fat_file_t f;
        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("NOFILE.TXT", &f), -1, "Absent file is not found");
        ASSERT_EQ(f.is_open, 0, "Handle untouched on failed open");
        ASSERT_EQ(fat_open("OLDFILE.TXT", &f), -1, "Deleted entry is not found");
        ASSERT_EQ(fat_open(0, &f), -1, "NULL path rejected");
        ASSERT_EQ(fat_open("TEST.TXT", 0), -1, "NULL handle rejected");
    END_TEST_CASE()

    // A 4-cluster file: FAT chain walk + byte-exact chunked reads
    TEST_CASE(multicluster_chain_and_content)
        ASSERT_EQ(mount_fresh_volume(), 0, "Volume mounts");

        fat_file_t f;
        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("HELLO.ELF", &f), 0, "Multi-cluster file opens");
        ASSERT_EQ(f.size, HELLO_LEN, "Size spans multiple clusters");

        // Walk the cluster chain exactly as the driver does
        uint32_t cluster = f.first_cluster;
        int chain_len = 0;
        while (cluster != 0xFFFFFFFFu && chain_len < 16) {
            chain_len++;
            cluster = fat_get_next_cluster(cluster);
        }
        ASSERT_EQ(chain_len, 4, "2000 bytes occupy 4 clusters of 512 bytes");

        // Read in awkward 300-byte chunks to cross cluster boundaries
        static uint8_t out[HELLO_LEN];
        memset(out, 0, sizeof(out));
        uint32_t total = 0;
        for (;;) {
            int n = fat_read(&f, out + total, 300);
            ASSERT_TRUE(n >= 0, "Chunked read never errors");
            if (n <= 0) {
                break;
            }
            total += (uint32_t)n;
        }
        ASSERT_EQ(total, HELLO_LEN, "Chunked reads deliver the whole file");
        ASSERT_EQ(memcmp(out, hello_bytes, HELLO_LEN), 0,
                  "Reassembled content matches byte-for-byte");
        fat_close(&f);
    END_TEST_CASE()

    // fat_seek repositions inside the cluster chain
    TEST_CASE(seek_and_partial_read)
        ASSERT_EQ(mount_fresh_volume(), 0, "Volume mounts");

        fat_file_t f;
        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("HELLO.ELF", &f), 0, "File opens");

        uint8_t buf[100];
        ASSERT_EQ(fat_seek(&f, 1500), 0, "Seek into the 3rd cluster");
        ASSERT_EQ(fat_read(&f, buf, sizeof(buf)), 100, "Read after seek");
        int mismatch = 0;
        for (uint32_t i = 0; i < sizeof(buf); i++) {
            if (buf[i] != hello_byte_at(1500 + i)) {
                mismatch = 1;
            }
        }
        ASSERT_EQ(mismatch, 0, "Bytes 1500..1599 match the source data");

        ASSERT_EQ(fat_seek(&f, 0), 0, "Rewind to start");
        ASSERT_EQ(fat_read(&f, buf, 4), 4, "Read the first bytes again");
        ASSERT_TRUE(buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' &&
                    buf[3] == 'F', "ELF magic read back after rewind");

        ASSERT_EQ(fat_seek(&f, HELLO_LEN + 4000), 0, "Seek past EOF is clamped");
        ASSERT_EQ(f.position, HELLO_LEN, "Position clamped to file size");
        ASSERT_EQ(fat_read(&f, buf, sizeof(buf)), 0, "Read at EOF returns 0");

        fat_close(&f);
        ASSERT_EQ(fat_seek(&f, 0), -1, "Seek on closed handle fails");
    END_TEST_CASE()

    // Root directory listing returns the injected files with sizes
    TEST_CASE(list_root_dir)
        ASSERT_EQ(mount_fresh_volume(), 0, "Volume mounts");

        fat_dir_entry_t entries[16];
        memset(entries, 0, sizeof(entries));
        int n = fat_list_dir("/", entries, 16);

        // The volume label (explicit FAT_ATTR_VOLUME_ID skip) and the
        // deleted entry are skipped; only the two real files remain.
        ASSERT_EQ(n, 2, "Two real files listed");
        ASSERT_EQ(memcmp(entries[0].name, "TEST    TXT", 11), 0,
                  "First entry is TEST.TXT in 8.3 form");
        ASSERT_EQ(entries[0].file_size, TEST_TXT_LEN, "Listed size matches");
        ASSERT_EQ(memcmp(entries[1].name, "HELLO   ELF", 11), 0,
                  "Second entry is HELLO.ELF in 8.3 form");
        ASSERT_EQ(entries[1].file_size, HELLO_LEN, "Listed size matches");

        ASSERT_EQ(fat_list_dir("/", entries, 1), 1, "max_entries is honoured");
        ASSERT_EQ(fat_list_dir("/", 0, 16), -1, "NULL output array rejected");
    END_TEST_CASE()

    // READ_ONLY/HIDDEN/SYSTEM files are real files: fat_open must find
    // them and fat_list_dir must list them; only true LFN slots
    // ((attr & 0x3F) == 0x0F) are skipped. Regression for roadmap bug #4:
    // the driver tested `attr & FAT_ATTR_LONG_NAME` (0x0F), which is
    // truthy for ANY of the R/H/S/V bits and hid such files entirely.
    TEST_CASE(attribute_flags_do_not_hide_files)
        ASSERT_EQ(mount_fresh_volume(), 0, "Volume mounts");

        static const char ro_data[]  = "read-only";
        static const char hid_data[] = "hidden";
        static const char sys_data[] = "system";
        ASSERT_EQ(fatdisk_add_file_attr("LOCKED.TXT", ro_data, 9,
                                        FAT_ATTR_READ_ONLY), 0,
                  "READ_ONLY file injected");
        ASSERT_EQ(fatdisk_add_file_attr("GHOST.TXT", hid_data, 6,
                                        FAT_ATTR_HIDDEN), 0,
                  "HIDDEN file injected");
        ASSERT_EQ(fatdisk_add_file_attr("DRIVER.SYS", sys_data, 6,
                                        FAT_ATTR_SYSTEM | FAT_ATTR_HIDDEN), 0,
                  "SYSTEM|HIDDEN file injected");
        ASSERT_EQ(fatdisk_add_lfn_entry(), 0, "Synthetic LFN slot injected");

        fat_file_t f;
        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("LOCKED.TXT", &f), 0, "READ_ONLY file opens");
        ASSERT_EQ(f.attributes, FAT_ATTR_READ_ONLY, "R attribute preserved");
        char buf[16];
        ASSERT_EQ(fat_read(&f, buf, sizeof(buf)), 9, "READ_ONLY file reads");
        ASSERT_EQ(memcmp(buf, ro_data, 9), 0, "READ_ONLY content matches");
        fat_close(&f);

        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("GHOST.TXT", &f), 0, "HIDDEN file opens");
        ASSERT_EQ(f.attributes, FAT_ATTR_HIDDEN, "H attribute preserved");
        fat_close(&f);

        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("DRIVER.SYS", &f), 0, "SYSTEM|HIDDEN file opens");
        ASSERT_EQ(f.attributes, FAT_ATTR_SYSTEM | FAT_ATTR_HIDDEN,
                  "S|H attributes preserved");
        fat_close(&f);

        // Listing: TEST.TXT + HELLO.ELF + the three attribute-flagged
        // files. The volume label and the LFN slot must NOT appear.
        fat_dir_entry_t entries[16];
        memset(entries, 0, sizeof(entries));
        int n = fat_list_dir("/", entries, 16);
        ASSERT_EQ(n, 5, "Five real files listed; label and LFN skipped");

        int saw_ro = 0, saw_hid = 0, saw_sys = 0, saw_label = 0, saw_lfn = 0;
        for (int i = 0; i < n; i++) {
            if (memcmp(entries[i].name, "LOCKED  TXT", 11) == 0) saw_ro = 1;
            if (memcmp(entries[i].name, "GHOST   TXT", 11) == 0) saw_hid = 1;
            if (memcmp(entries[i].name, "DRIVER  SYS", 11) == 0) saw_sys = 1;
            if (entries[i].attributes & FAT_ATTR_VOLUME_ID) saw_label = 1;
            if ((entries[i].attributes & FAT_ATTR_LFN_MASK)
                    == FAT_ATTR_LONG_NAME) saw_lfn = 1;
        }
        ASSERT_EQ(saw_ro, 1, "READ_ONLY file listed");
        ASSERT_EQ(saw_hid, 1, "HIDDEN file listed");
        ASSERT_EQ(saw_sys, 1, "SYSTEM file listed");
        ASSERT_EQ(saw_label, 0, "Volume label not listed");
        ASSERT_EQ(saw_lfn, 0, "LFN slot not listed");
    END_TEST_CASE()

    // Sector I/O round-trips through the driver; fat_write is documented off
    TEST_CASE(sector_io_and_write_contract)
        ASSERT_EQ(mount_fresh_volume(), 0, "Volume mounts");

        uint8_t pattern[512], readback[512];
        for (int i = 0; i < 512; i++) {
            pattern[i] = (uint8_t)(i ^ 0x5C);
        }
        // Sector 4000 is unused data area on the fresh volume
        ASSERT_EQ(fat_write_sector(4000, pattern), 0, "Sector write succeeds");
        memset(readback, 0, sizeof(readback));
        ASSERT_EQ(fat_read_sector(4000, readback), 0, "Sector read succeeds");
        ASSERT_EQ(memcmp(pattern, readback, 512), 0, "Sector round-trips exactly");

        ASSERT_EQ(fat_read_sector(FATDISK_TOTAL_SECTORS + 5, readback), -1,
                  "Out-of-range read propagates the IDE error");
        ASSERT_EQ(fat_write_sector(FATDISK_TOTAL_SECTORS + 5, pattern), -1,
                  "Out-of-range write propagates the IDE error");
        ASSERT_EQ(fat_read_sector(100, 0), -1, "NULL buffer rejected");

        // File-level writes are not implemented in kernel/fat.c: the
        // filesystem is read-only at file granularity. Pin that down.
        fat_file_t f;
        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("TEST.TXT", &f), 0, "File opens");
        ASSERT_EQ(fat_write(&f, "x", 1), -1, "fat_write reports not-implemented");
        fat_close(&f);
    END_TEST_CASE()

    // Unmount invalidates the driver state until the next mount
    TEST_CASE(unmount_lifecycle)
        ASSERT_EQ(mount_fresh_volume(), 0, "Volume mounts");

        ASSERT_EQ(fat_unmount(), 0, "Unmount succeeds");
        ASSERT_EQ(fat_is_mounted(), 0, "Driver reports unmounted");
        ASSERT_EQ(fat_unmount(), -1, "Double unmount fails");
        ASSERT_TRUE(fat_get_info() == 0, "No info after unmount");

        fat_file_t f;
        memset(&f, 0, sizeof(f));
        ASSERT_EQ(fat_open("TEST.TXT", &f), -1, "Open fails after unmount");
        ASSERT_EQ(fat_get_next_cluster(2), 0xFFFFFFFFu,
                  "Chain walk refuses to run unmounted");

        ASSERT_EQ(fat_mount(0), 0, "Remount succeeds");
        ASSERT_EQ(fat_open("TEST.TXT", &f), 0, "Files reachable again");
        fat_close(&f);
    END_TEST_CASE()

END_TEST_SUITE()
