# TocinOS Filesystem Support Plan

## Overview

TocinOS will support multiple filesystem types to provide compatibility with various storage media and use cases. The initial implementation focuses on FAT filesystems due to their simplicity and widespread use, with plans for more advanced filesystems in future releases.

## Phase 1: FAT Filesystem Family (v1.1 - v1.5)

### FAT12
**Target**: Floppy disks and small storage media
**Status**: 🎯 Planned

#### Features
- 12-bit File Allocation Table
- Maximum volume size: 32 MB
- Typical use: 1.44 MB floppy disks
- Cluster sizes: 512 bytes to 4 KB

#### Implementation Priority
- **High**: Essential for bootable floppy disk images
- Enables loading kernel modules from floppy
- Provides basic file operations for initial development

### FAT16
**Target**: Small hard drives, USB drives
**Status**: 🎯 Planned

#### Features
- 16-bit File Allocation Table
- Maximum volume size: 2 GB (with 32 KB clusters) or 4 GB (with 64 KB clusters)
- Better space efficiency than FAT12
- Cluster sizes: 2 KB to 64 KB

### FAT32
**Target**: Large hard drives, USB drives, SD cards
**Status**: 🎯 Planned

#### Features
- 32-bit File Allocation Table (28 bits actually used)
- Maximum volume size: 2 TB (with Windows formatting) or 8 TB (theoretical)
- Maximum file size: 4 GB
- Cluster sizes: 512 bytes to 32 KB

#### Implementation Priority
- **High**: Most common filesystem for removable media
- Essential for practical storage use
- Wide compatibility

## Implementation Plan

### Core Operations API
```c
// Mount/Unmount
int fat_mount(const char *device, const char *mountpoint, int type);
int fat_unmount(const char *mountpoint);

// File Operations
int fat_open(const char *path, int flags);
int fat_close(int fd);
int fat_read(int fd, void *buffer, size_t size);
int fat_write(int fd, const void *buffer, size_t size);
```

## Phase 2: Modern Filesystems (v2.0+)

### Btrfs-inspired Custom FS
**Status**: 🔮 Long-term goal

#### Planned Features
- Copy-on-Write (CoW)
- Built-in snapshots
- Transparent compression
- Checksums for data integrity
- Deduplication
- Sub-volumes

**Timeline**: v3.0+

## References

- [Microsoft FAT32 File System Specification](https://academy.cba.mit.edu/classes/networking_communications/SD/FAT.pdf)
- [OSDev Wiki: FAT](https://wiki.osdev.org/FAT)

---

This plan provides a clear roadmap for implementing filesystem support in TocinOS.
