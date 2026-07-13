# TocinOS Boot Protocol — `tocinboot_info` v1

**Status:** Frozen at v1 (this document precedes and governs the implementation, per
ROADMAP §4.1 "spec before ABIs").
**Implemented by:** TocinBoot UEFI loader (`boot/uefi/`), and — as an M1 follow-up — the
BIOS stage2 (`boot/stage2/`). Both paths hand the kernel **exactly one entry contract**.
**Header:** `include/boot/tocinboot.h` is the normative C encoding of this document. If
the header and this document ever disagree, this document wins and the header is a bug.

---

## 1. Overview

TocinBoot loads the TocinOS kernel (a plain ELF executable), builds a single physically
addressed information block — `tocinboot_info` — describing the machine, and jumps to the
kernel entry point with a magic value and the block's physical address in registers.

Design rules:

- **One struct, two producers** (UEFI loader, BIOS stage2), **one consumer** (the kernel).
- **Physical addresses only.** Every `*_addr` field is a physical address stored as a
  64-bit little-endian integer, regardless of entry bitness.
- **Little-endian, naturally aligned, no implicit padding.** Every field sits at an
  offset that is a multiple of its size; all padding is explicit `reserved` fields. The
  layout is therefore identical on i386 and x86-64 with or without `packed` attributes.
  `include/boot/tocinboot.h` pins every offset with static assertions.
- **Forward compatible** (§8): published field offsets never change; new fields only
  ever take over `reserved` space or extend the tail.

---

## 2. Magic numbers

| Name | Value | Where |
|---|---|---|
| `TOCINBOOT_INFO_MAGIC` | `0x31494254` (`"TBI1"` as bytes in memory) | `tocinboot_info.magic` |
| `TOCINBOOT_REG_MAGIC` | `0x70C1B007` | `EAX` (32-bit entry) / `RAX` (64-bit entry) at handoff |

A kernel MUST verify **both** magics before trusting anything else. If either is wrong,
the kernel was not entered via this protocol (e.g. legacy multiboot QEMU `-kernel` boot)
and MUST fall back to its legacy assumptions.

---

## 3. `tocinboot_info` layout (232 bytes)

All integers little-endian. `u8/u16/u32/u64` are fixed-width unsigned integers.

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0x00 | u32 | `magic` | `TOCINBOOT_INFO_MAGIC` (`0x31494254`) |
| 0x04 | u32 | `version` | Protocol version. This document: **1** |
| 0x08 | u32 | `size` | Total bytes written by the loader (v1: **232**) |
| 0x0C | u32 | `flags` | Bit field, §3.1 |
| 0x10 | u64 | `memmap_addr` | Physical address of the memory map array (§4) |
| 0x18 | u32 | `memmap_count` | Number of memory map entries |
| 0x1C | u32 | `memmap_entry_size` | Stride between entries in bytes (v1: **24**) |
| 0x20 | u64 | `fb_base` | Framebuffer physical base (§5) |
| 0x28 | u32 | `fb_width` | Visible pixels per row |
| 0x2C | u32 | `fb_height` | Visible rows |
| 0x30 | u32 | `fb_pitch` | **Bytes** per row (≥ width × bytes-per-pixel) |
| 0x34 | u32 | `fb_bpp` | Bits per pixel (32 for formats 1 and 2) |
| 0x38 | u32 | `fb_format` | Pixel format enum, §5.1 |
| 0x3C | u8 | `fb_red_size` | Red mask width in bits (format 3; else informative) |
| 0x3D | u8 | `fb_red_shift` | Red mask LSB position |
| 0x3E | u8 | `fb_green_size` | Green mask width |
| 0x3F | u8 | `fb_green_shift` | Green mask LSB position |
| 0x40 | u8 | `fb_blue_size` | Blue mask width |
| 0x41 | u8 | `fb_blue_shift` | Blue mask LSB position |
| 0x42 | u8 | `fb_rsvd_size` | Reserved-channel mask width |
| 0x43 | u8 | `fb_rsvd_shift` | Reserved-channel mask LSB position |
| 0x44 | u32 | `reserved0` | MUST be 0 |
| 0x48 | u64 | `rsdp_addr` | Physical address of the ACPI RSDP, 0 if none (§6) |
| 0x50 | u64 | `cmdline_addr` | Physical address of the command line, 0 if none (§7) |
| 0x58 | u32 | `cmdline_len` | Command line length in bytes, excluding the NUL |
| 0x5C | u32 | `reserved1` | MUST be 0 |
| 0x60 | u64 | `initrd_addr` | Physical address of the initrd/module, 0 if none (§7) |
| 0x68 | u64 | `initrd_size` | Initrd size in bytes |
| 0x70 | u64 | `kernel_phys_base` | First byte of the loaded kernel image (page-rounded down) |
| 0x78 | u64 | `kernel_phys_end` | One past the last byte (page-rounded up) |
| 0x80 | u64 | `kernel_entry` | Entry point address the loader jumped to (`e_entry`) |
| 0x88 | char[32] | `loader_name` | NUL-terminated ASCII, e.g. `"TocinBoot 0.1 (UEFI)"` |
| 0xA8 | u64[8] | `reserved2` | MUST be 0 |

Total size: **0xE8 = 232 bytes**.

### 3.1 `flags`

| Bit | Name | Meaning |
|---|---|---|
| 0 | `TOCINBOOT_F_UEFI` | Booted via the UEFI path |
| 1 | `TOCINBOOT_F_BIOS` | Booted via the BIOS path (mutually exclusive with bit 0) |
| 2 | `TOCINBOOT_F_FB` | Framebuffer fields (0x20–0x43) are valid |
| 3 | `TOCINBOOT_F_RSDP` | `rsdp_addr` is valid and nonzero |
| 4 | `TOCINBOOT_F_CMDLINE` | `cmdline_addr/len` are valid |
| 5 | `TOCINBOOT_F_INITRD` | `initrd_addr/size` are valid |
| 6–31 | — | Reserved; loader writes 0, consumers MUST ignore unknown bits |

### 3.2 Placement of loader-provided objects

- `tocinboot_info` itself: physically contiguous, **4 KiB aligned**, **below 4 GiB**,
  inside a `BOOTLOADER`-type region (§4.1).
- Memory map array: contiguous, 8-byte aligned, below 4 GiB, `BOOTLOADER` region.
- Command line: below 4 GiB, UTF-8, NUL-terminated.
- Initrd: page-aligned. v1 loaders always place it below 4 GiB (required whenever the
  kernel is entered via the 32-bit contract).
- All of these live in memory the kernel will eventually reclaim (§4.2); the kernel MUST
  copy out anything it needs long-term before reusing `BOOTLOADER` regions.

---

## 4. Physical memory map

`memmap_addr` points to `memmap_count` entries, each `memmap_entry_size` bytes apart
(iterate by the stride, never by `sizeof` — future versions may grow the entry; its first
24 bytes are frozen):

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0x00 | u64 | `base` | Physical start address |
| 0x08 | u64 | `length` | Length in bytes (never 0) |
| 0x10 | u32 | `type` | §4.1 |
| 0x14 | u32 | `reserved` | MUST be 0 |

Guarantees: entries are sorted by ascending `base`, do not overlap, and adjacent entries
of the same type are coalesced. The map covers everything the firmware reported; TocinBoot
neither invents nor hides ranges (it only normalizes types). Device MMIO — including,
possibly, the framebuffer — is not guaranteed to appear.

### 4.1 Normalized memory types (`TOCINBOOT_MEM_*`)

| Value | Name | Kernel may use as RAM? |
|---|---|---|
| 0 | `INVALID` | never emitted |
| 1 | `USABLE` | yes (see 64-bit caveat, §6.3) |
| 2 | `RESERVED` | no |
| 3 | `ACPI_RECLAIMABLE` | after ACPI tables are consumed |
| 4 | `ACPI_NVS` | no |
| 5 | `BAD` | no |
| 6 | `BOOTLOADER` | after boot info is consumed — see §4.2 |
| 7 | `KERNEL_MODULES` | it *is* the kernel/initrd; reusable only by the kernel's own accounting |
| 8 | `FRAMEBUFFER` | no (device memory) |

Producer mapping — UEFI (`GetMemoryMap` types):

| EFI type | Normalized |
|---|---|
| `EfiConventionalMemory`, `EfiBootServicesCode`, `EfiBootServicesData` | `USABLE` |
| `EfiLoaderCode`, `EfiLoaderData` | `BOOTLOADER` |
| `EfiACPIReclaimMemory` | `ACPI_RECLAIMABLE` |
| `EfiACPIMemoryNVS` | `ACPI_NVS` |
| `EfiUnusableMemory` | `BAD` |
| `EfiRuntimeServicesCode/Data`, `EfiReservedMemoryType`, `EfiMemoryMappedIO`, `EfiMemoryMappedIOPortSpace`, `EfiPalCode`, `EfiPersistentMemory`, anything unknown | `RESERVED` |

Producer mapping — BIOS E820 (follow-up implementation): 1→`USABLE`, 2→`RESERVED`,
3→`ACPI_RECLAIMABLE`, 4→`ACPI_NVS`, 5→`BAD`, unknown→`RESERVED`; ranges the stage2
itself occupies at handoff are reported as `BOOTLOADER`.

### 4.2 The `BOOTLOADER` reclaim rule

`BOOTLOADER` regions contain, at handoff: `tocinboot_info`, the memory map array, the
command line, the loader's GDT and handoff stack, **and (in TocinBoot v0.1) the loaded
kernel image and initrd** (the v1 UEFI loader allocates them as `EfiLoaderData` and does
not carve them into distinct `KERNEL_MODULES` entries; the kernel's own extent is given
exactly by `kernel_phys_base/end`, the initrd's by `initrd_addr/size`).

Rule for consumers: treat every `BOOTLOADER` byte as in-use until you (a) have copied out
all boot information you still need, and (b) exclude `[kernel_phys_base, kernel_phys_end)`
and the initrd range from reclamation. Producers MAY instead emit distinct
`KERNEL_MODULES` entries for those ranges; consumers MUST handle both presentations.

---

## 5. Framebuffer

Valid only when `TOCINBOOT_F_FB` is set. Sourced from UEFI GOP (or VBE on the BIOS path).
The framebuffer is linear; the address is physical device memory — map it uncached or
write-combining. `fb_pitch` is in bytes and may exceed `fb_width × fb_bpp/8`.

### 5.1 `fb_format`

| Value | Name | One pixel, read as little-endian u32 | Byte order in memory |
|---|---|---|---|
| 0 | `NONE` | — | — |
| 1 | `XRGB32` | `0x00RRGGBB` | B, G, R, X (GOP `PixelBlueGreenRedReserved8BitPerColor`) |
| 2 | `XBGR32` | `0x00BBGGRR` | R, G, B, X (GOP `PixelRedGreenBlueReserved8BitPerColor`) |
| 3 | `BITMASK` | per `fb_*_size/shift` fields | per masks |

For formats 1 and 2 the mask fields are filled in redundantly (informative). Format 3 is
emitted only when the firmware reports `PixelBitMask`.

---

## 6. Machine state at kernel entry

Common to both entries:

- `ExitBootServices()` has been called (UEFI path): **no boot services exist.** Runtime
  services have **not** been virtualized (`SetVirtualAddressMap` was not called); v1
  kernels MUST NOT call EFI runtime services.
- Interrupts disabled (`IF=0`), `DF=0`, `TF=0`. NMI/SMI are not masked.
- A20 is enabled.
- PIC/APIC/PIT/RTC are in whatever state firmware left; the kernel fully reinitializes
  interrupt controllers before `sti`. The UEFI firmware watchdog has been disabled.
- GDTR points to a valid loader-owned GDT located in `BOOTLOADER` memory. Segment
  selector **values** are unspecified — the kernel MUST load its own GDT before it can
  rely on specific selectors, and before reclaiming `BOOTLOADER` memory.
- IDTR contents are undefined. The kernel MUST `lidt` before `sti`.
- FPU/SSE state is as firmware left it; the kernel owns `CR0.TS/EM/MP`, `CR4.OSFXSR`, etc.
- The stack is loader-provided, at least 16 KiB, 16-byte aligned at entry, in
  `BOOTLOADER` memory. Entry is by `jmp`, not `call`: **there is no return address; the
  entry point must never return.**
- All registers not listed as defined below are undefined.

### 6.1 32-bit protected-mode entry (used for ELF32 kernels)

| Item | Guarantee |
|---|---|
| Mode | 32-bit protected mode. `CR0.PE=1`, **`CR0.PG=0`** (paging off) |
| Long mode | Fully exited: `EFER.LME=0`, `EFER.LMA=0`, `CR4.PAE=0` |
| `EAX` | `0x70C1B007` (`TOCINBOOT_REG_MAGIC`) |
| `EBX` | Physical address of `tocinboot_info` (< 4 GiB) |
| `CS` | Flat 32-bit code segment: base 0, limit 4 GiB, D=1 |
| `DS,ES,FS,GS,SS` | Flat 32-bit data segments: base 0, limit 4 GiB |
| `ESP` | Loader stack top (< 4 GiB) |

With paging off, all addresses in `tocinboot_info` are directly dereferenceable
(everything the kernel must read is below 4 GiB, §3.2).

### 6.2 64-bit long-mode entry (used for ELF64 kernels)

| Item | Guarantee |
|---|---|
| Mode | 64-bit long mode (`LMA=1`), 64-bit code segment (L=1, D=0), paging on |
| `RAX` | `0x70C1B007` (`TOCINBOOT_REG_MAGIC`) |
| `RDI` | Physical address of `tocinboot_info` (also the SysV first argument) |
| `RSP` | Loader stack top, 16-byte aligned |
| `CR3` | Loader-provided page tables **identity-mapping**, writable, at minimum: every region in the memory map and the framebuffer |

### 6.3 64-bit paging caveat

The active page tables at 64-bit entry are loader-provided and may physically reside in
regions the map calls `USABLE` (e.g. inherited firmware tables in former
`EfiBootServicesData`). Therefore: **the kernel MUST load its own page tables (its own
CR3) before writing to `USABLE` memory it did not allocate.** A future protocol revision
may tighten this by requiring loader tables to live in `BOOTLOADER` memory. (The 32-bit
entry has no such caveat: paging is off.)

---

## 7. Kernel image, command line, initrd (UEFI loader behavior)

- Kernel: read from the boot ESP at `\EFI\TOCINOS\KERNEL.ELF`. Plain ELF32 or ELF64
  (`ET_EXEC`, `EM_386` or `EM_X86_64`). Every `PT_LOAD` segment is loaded at its exact
  `p_paddr` (`p_align` is not honored for placement), `p_memsz - p_filesz` tail
  zero-filled. The loader allocates the covering physical span with
  `AllocatePages(AllocateAddress)`; if the firmware owns any of it, loading fails with a
  diagnostic naming the range. `e_entry` is interpreted as a physical address.
- Command line: optional file `\EFI\TOCINOS\CMDLINE.TXT` (UTF-8; a single trailing
  CR/LF is stripped). Absent file ⇒ flag clear, `cmdline_addr=0`.
- Initrd: optional file `\EFI\TOCINOS\INITRD.IMG`, loaded whole, page-aligned, below
  4 GiB. Absent ⇒ flag clear.
- RSDP: taken from the EFI configuration table — ACPI 2.0 GUID preferred, ACPI 1.0
  accepted; absent ⇒ `rsdp_addr=0`, flag clear.

---

## 8. Forward compatibility rules

1. **Offsets 0x00–0x0B (`magic`, `version`, `size`) are frozen forever.**
2. Every published field's offset and meaning is frozen once released. New fields may
   only replace `reserved*` space or extend the struct tail; either way `size` grows or
   stays, never shrinks, and `version` is incremented.
3. A consumer built against version N MUST accept `version ≥ N` (everything it knows is
   still where it was) and MUST NOT read past `min(size, sizeof(its own view))`.
4. An **incompatible** relayout requires a new magic (`"TBI2"`), not a version bump.
5. Producers MUST zero all reserved fields and unknown-to-them flag bits; consumers MUST
   ignore reserved fields and unknown flag bits.
6. The memory map MUST be iterated with `memmap_entry_size` as the stride; the first 24
   bytes of an entry are frozen.

---

## 9. Compatibility & implementation status

Chosen v1 handoff implementation (TocinBoot v0.1, UEFI): the current TocinOS kernel is
ELF32 linked at 1 MiB, so the loader implements the **32-bit protected-mode entry fully**
— after `ExitBootServices` it drops from long mode to paging-off protected mode via a
relocatable trampoline (loader-built GDT in `BOOTLOADER` memory) and jumps to `e_entry`
with `EAX/EBX` per §6.1.

The 64-bit contract (§6.2) is **specified but not yet exercised**: there is no ELF64
TocinOS kernel until M2. TocinBoot v0.1 detects an ELF64 kernel, prints
`"ELF64 kernel: 64-bit long-mode handoff lands with the M2 kernel"`, and stops cleanly
rather than jumping down an untested path. This is the single place TocinBoot v0.1
deviates from full protocol coverage, and it disappears in M2.

The kernel itself does not yet *consume* `tocinboot_info` (its multiboot-era entry
ignores registers) — kernel-side consumption, the GOP splash, and the BIOS-stage2
producer are the remaining M1 work items. Because the kernel's entry ignores `EAX/EBX`
today, handing off per this protocol is already safe: the same `kernel.elf` boots
unchanged via BIOS MBR+stage2, QEMU `-kernel`, and TocinBoot/UEFI.

Legacy note: the pre-M1 headers `include/boot/boot_info.h` (BIOS `boot_info_t`, still
referenced by `kernel/drivers/vesa_driver.c`) remain until the kernel is switched to
`tocinboot_info`; new code MUST use `include/boot/tocinboot.h` only.
