/**
 * TocinOS ls command
 * 
 * Lists directory contents
 * Usage: ls [directory]
 */

#include "libc/libc.h"

// FAT file attributes
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20

// Convert 8.3 filename to readable format
static void format_filename(const char *fat_name, char *out) {
    int i, j = 0;
    
    // Copy name part (8 chars)
    for (i = 0; i < 8 && fat_name[i] != ' '; i++) {
        out[j++] = fat_name[i];
    }
    
    // Add extension if present
    if (fat_name[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && fat_name[i] != ' '; i++) {
            out[j++] = fat_name[i];
        }
    }
    
    out[j] = '\0';
}

void _start(void) {
    dirent_t entries[32];
    char filename[16];
    
    printf("Directory listing of /\n");
    printf("----------------------\n");
    
    // Open and read directory
    DIR *dir = opendir("/");
    if (!dir) {
        printf("ls: cannot open directory\n");
        exit(1);
    }
    
    int count = 0;
    dirent_t *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip volume label and hidden files
        if (entry->attr & FAT_ATTR_VOLUME_ID) continue;
        if (entry->attr & FAT_ATTR_HIDDEN) continue;
        
        format_filename(entry->name, filename);
        
        // Print entry with type indicator
        if (entry->attr & FAT_ATTR_DIRECTORY) {
            printf("  %s/\n", filename);
        } else {
            printf("  %s  %u bytes\n", filename, entry->size);
        }
        count++;
    }
    
    closedir(dir);
    
    printf("----------------------\n");
    printf("%d files\n", count);
    
    exit(0);
}
