# TocinBoot v0.1 — UEFI loader

The UEFI producer of the TocinOS boot protocol. Loads the ELF32 kernel from
the ESP, builds a `tocinboot_info` v1 block (spec: `docs/BOOT_PROTOCOL.md`,
header: `include/boot/tocinboot.h`), exits boot services, drops the CPU from
64-bit long mode to paging-off 32-bit protected mode, and jumps to the kernel
entry with `EAX=0x70C1B007`, `EBX=&tocinboot_info` (spec §6.1).

## Files

| File | Role |
|---|---|
| `tocinboot.c` | The loader: GOP, ESP file I/O, ELF32 placement, info block, memory-map normalization, EBS dance, handoff |
| `handoff32.asm` | Position-independent 64→32 trampoline blob (`nasm -f bin`), embedded into the loader as a byte array |
| `efi.h` | Minimal self-contained UEFI definitions (no gnu-efi dependency) |
| `Makefile` | Standalone build (`make -C boot/uefi`) |
| `test-ovmf.sh` | Headless OVMF boot test |

## Build & test

Prerequisites: `gcc-mingw-w64-x86-64`, `nasm`, `mtools`, `qemu-system-x86_64`,
`ovmf` (no root needed; the FAT image is assembled with mtools).

```sh
make                 # at the repo root: build/kernel.elf (ELF32)
make -C boot/uefi    # build/uefi/TOCINBOOT.EFI
make -C boot/uefi img   # build/uefi/esp.img (BOOTX64.EFI + KERNEL.ELF)
make -C boot/uefi test  # boot under OVMF, grep serial for both banners
```

The test passes when the serial log contains `TocinBoot v0.1` (loader ran)
and — if `KERNEL.ELF` was staged — `=== TocinOS Booting ===` (the kernel's
first COM1 line, proving the full 64-bit UEFI → 32-bit kernel handoff).

ESP layout (spec §7): `\EFI\BOOT\BOOTX64.EFI` (the loader),
`\EFI\TOCINOS\KERNEL.ELF` (required), `\EFI\TOCINOS\CMDLINE.TXT` and
`\EFI\TOCINOS\INITRD.IMG` (optional; add them to `build/uefi/esp/EFI/TOCINOS/`
and re-run `make -C boot/uefi img`, or `mcopy` them into `esp.img` directly).

## How the handoff works

1. All allocations are `EfiLoaderCode/Data` below 4 GiB → normalized to
   `BOOTLOADER` in the memory map (spec §4.1/§4.2). The kernel span is claimed
   at its exact physical address with `AllocatePages(AllocateAddress)`; BSS
   tails are zero-filled.
2. A 9-page data block holds `tocinboot_info` (page 0, 4 KiB aligned), the
   normalized memory-map array (4 pages), and the 16 KiB handoff stack. A
   separate `EfiLoaderCode` page receives the trampoline blob, with the info
   pointer, kernel entry, and stack top patched into fixed slots at 0xC0.
3. EBS discipline: every summary is printed *before* the final
   `GetMemoryMap`; the raw-map buffer is preallocated with slack; between the
   final `GetMemoryMap` and `ExitBootServices` there is only pure in-memory
   normalization (sort + coalesce + EFI→tocinboot type mapping); one retry on
   a stale map key. After EBS, output is raw COM1 (port 0x3F8) only.
4. The trampoline (still in long mode, identity-paged): `lgdt` a self-patched
   flat GDT inside its own BOOTLOADER page, far-return into a 32-bit code
   segment (compatibility mode), clear `CR0.PG` (hardware clears `EFER.LMA`),
   clear `EFER.LME` and `CR4.PAE`, reload flat data segments, `cld`, load
   `EAX` with the register magic (after `rdmsr` clobbered it), `jmp` — not
   call — to `e_entry`.

## Status

Verified under OVMF (QEMU `-machine pc`): the loader boots the current
`build/kernel.elf` all the way into `kernel_main`, which runs its full init
sequence on serial. ELF64 kernels stop cleanly with
`ELF64 kernel: 64-bit long-mode handoff lands with the M2 kernel` (spec §9).
A missing `KERNEL.ELF` produces a diagnostic naming the path and status.

## Limitations (v0.1, by design)

- **ELF64 = clean stop, not a handoff.** The 64-bit long-mode contract
  (spec §6.2) is specified but unexercised until the M2 kernel exists.
- **Identity-map assumption pre-handoff.** The loader dereferences physical
  addresses directly while boot services are up (UEFI guarantees identity
  mapping) and requires the whole kernel span + info block below 4 GiB.
- Kernel and initrd stay inside `BOOTLOADER` regions rather than distinct
  `KERNEL_MODULES` entries; their extents are published via
  `kernel_phys_base/end` and `initrd_addr/size` (allowed by spec §4.2).
- The GOP mode is whatever the firmware left active; no mode setting.
- EFI runtime services are not virtualized and must not be called (spec §6).

## Next steps (M1 follow-ups)

- Kernel consumes `tocinboot_info` (verify both magics, use the memory map,
  framebuffer, RSDP, cmdline) and retires `include/boot/boot_info.h`.
- BIOS stage2 emits the same struct (`TOCINBOOT_F_BIOS`, E820 mapping per
  spec §4.1) so both paths converge on one entry contract.
- M2: ELF64 kernel + the 64-bit long-mode handoff (loader-built identity
  page tables, `RDI=&info`, spec §6.2/§6.3).
