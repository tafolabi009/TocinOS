# TocinOS — General-Purpose OS Roadmap

**Status:** Living document. Supersedes the "Post-v2.0 Roadmap" section of README.md.
**Goal:** Evolve TocinOS from a learning kernel into a general-purpose operating system with
a custom bootloader, a petabyte-scale filesystem, a native executable/packaging format,
Linux application compatibility, a full driver stack, and a smooth, low-footprint GUI —
developed in **C, C++, and Tocin** (https://github.com/tafolabi009/tocinlang).

---

## 1. Where we are (honest baseline)

The working core today: BIOS two-stage boot → 32-bit protected mode → PMM/VMM (bitmap +
basic paging) → GDT/IDT/ISR → PIT timer → PS/2 keyboard → serial → `int 0x80` syscalls →
FAT16/32 on IDE → ELF loader (with real `PT_INTERP` interpreter support) → user-mode
programs → shell.

Known debt (verified by audit, Feb 2026):

| Area | State |
|------|-------|
| Advanced MM (`buddy`, `slab`, `swap`, `demand`, `page_cache`, `vma`) | Compiled, **never initialized** |
| CFS scheduler (`kernel/task/cfs.c`) | Compiled, **not used** (scheduler.c is live) |
| Networking (`kernel/tcpip.c` + `kernel/net/`) | Two stacks; now unified at the IP layer, **no socket syscalls, no RX path from driver, inits not called at boot** |
| NIC drivers (e1000, rtl8139, virtio-net) | Present, **not initialized at boot** |
| USB stack (`kernel/drivers/usb/`) | Present and initialized, **UHCI only, unverified on hardware** |
| pthreads | Syscalls registered; **`thread_clone` is a stub** (no task creation, TLS not in GDT) |
| Dynamic linker (`user/ld.so`) | Real implementation; **never installed on disk image, no dynamic programs built** |
| ext2/ext4, tocinfs, fs_cache | Large code volume, **partially or not wired** |
| Unit tests | 16 green, but they test **mocks**, not kernel code |
| x86-64 | Kernel boots via same entry, **32-bit userspace only** |

Guiding rule going forward: **no feature counts as done until it is built, initialized at
boot, reachable from userspace, and exercised by a test.** Dormant code is either wired in
by a milestone below or deleted.

---

## 2. Architecture decisions

### 2.1 64-bit first
A general-purpose OS in 2026 is x86-64. The 32-bit path remains for QEMU-based bring-up,
but all new subsystems (TocinFS v2, NVMe, GUI, Linux compat) target long mode. Petabyte
storage arithmetic alone (64-bit LBAs, >4 GiB files) makes this non-negotiable.
64-bit userspace (syscall/sysret, 4-level paging) lands in **M2**.

### 2.2 Custom bootloader: **TocinBoot**
Two paths, one boot protocol:

- **UEFI path (primary):** a from-scratch UEFI application (`boot/uefi/`, rewritten):
  reads kernel + initrd from the ESP, obtains the memory map and GOP framebuffer, sets up
  identity + higher-half page tables, exits boot services, and hands off via a versioned
  `tocinboot_info` struct (memory map, framebuffer, RSDP, cmdline, initrd location).
- **BIOS path (legacy/QEMU):** the existing MBR + stage2, upgraded to produce the *same*
  `tocinboot_info` handoff so the kernel has exactly one entry contract.

Boot targets: < 1 s from firmware handoff to shell in QEMU; graphical splash via GOP.

### 2.3 Filesystem: **TocinFS v2** (petabyte-scale)
The current `tocinfs.c` is a placeholder and will be replaced by a real design:

- **64-bit everywhere:** 64-bit block addresses and file sizes. With 4 KiB blocks the
  format addresses 2^64 × 4 KiB; the *supported* target is **1 PiB volumes / 16 TiB files**,
  CI-tested via sparse loopback images.
- **Extent-based allocation** (not block lists) with B+tree metadata — the same family of
  decisions that lets ext4/XFS scale; extents keep metadata small for huge files.
- **Metadata journaling** (ordered mode) for crash consistency; full data journaling optional.
- **Copy-on-write snapshots** as a v2.1 stretch goal (design reserved in the on-disk format).
- Block layer below it: a proper `bio`-style request layer over AHCI/NVMe with a unified
  page cache (finally wiring `page_cache.c` or its successor).
- FAT16/32 stays for boot media and interchange; ext2 read support retained for tooling.

A userspace `mkfs.tocinfs` + `fsck.tocinfs` (written in **Tocin**, hosted mode) ship with
the format — the on-disk spec lives in `docs/TOCINFS_SPEC.md` before the first line of
kernel code (spec-first, like every serious FS).

### 2.4 Executables & packaging: **`.tox`** and **`.tap`**
- **`.tox` (TocinOS eXecutable):** ELF remains the binary container (every toolchain emits
  it; our loader already speaks it). `.tox` is ELF with a TocinOS identity: `PT_NOTE`
  segment `TOCIN` (ABI version, required capabilities) + registered file extension. The
  loader treats a missing note as a plain ELF (compat), a bad ABI version as a hard error.
- **`.tap` (TocinOS Application Package):** AppImage-style single-file apps: a `.tox`
  runtime stub prepended to a read-only filesystem image (initially FAT, later squashfs)
  containing the app, its libraries, icon, and manifest. The kernel loop-mounts the image
  and executes the embedded entry. One file = one app; delete to uninstall.
- **AppImage itself:** actual Linux AppImages additionally require the Linux compat layer
  (2.5) plus loop mounting + squashfs; they become runnable at **M6**, after which both
  `.tap` (native) and `.AppImage` (foreign) work side by side.

### 2.5 Linux application compatibility (the Linuxulator approach)
Like FreeBSD's Linux emulation: **no Linux code in the kernel — a syscall translation
personality.** ELF binaries whose OSABI/interpreter identify as Linux get a Linux syscall
table mapped onto TocinOS primitives.

Staged, each stage with a named acceptance binary:
1. **Static musl "hello"** — ~40 syscalls (mmap, brk, write, exit_group, futex single-thread).
2. **Static busybox** — full file/dir syscall surface, /proc basics, signals.
3. **Dynamic binaries** — Linux `ld-musl`/`ld-linux` loading via our interpreter support.
4. **Multithreaded** — real clone/futex/TLS (depends on M3 threads).
5. **AppImage** — loop mount + squashfs + FUSE-less direct mount path.

This is the single most demanding line item in the vision; it is deliberately late (M6)
because it depends on solid MM (M2), threads (M3), and VFS (M4).

### 2.6 Driver stack build-out
Priority order is dictated by the milestones, not novelty:

| Tier | Drivers | Why |
|------|---------|-----|
| 1 (M2–M4) | AHCI, NVMe, virtio-blk | Petabyte FS needs modern block devices; IDE caps at small/slow |
| 2 (M4–M5) | xHCI (USB3) + HID + mass storage | Real hardware input/boot media; replaces unverified UHCI |
| 3 (M7) | virtio-gpu, Intel HD framebuffer (GOP-inherited), Bochs VBE | GUI |
| 4 (M8) | e1000e/virtio-net RX/TX wired to the socket layer, rtl8139 kept for QEMU | Networking live |
| Cont. | AC'97/HDA audio, RTC, ACPI (shutdown/reboot first) | "Everything a normal OS has" |

### 2.7 GUI: smooth animations with a minimal footprint
- **Compositor-first design** ("TocinWM"): every window is a buffer; the compositor blends
  damage-tracked regions and page-flips on vsync. No X11-style in-place drawing.
- **Frame discipline:** 60 fps target = 16.6 ms budget; animations are time-based
  (interpolated by frame timestamp, never frame-counted), so they stay smooth under load.
- **Software rendering first, optimized:** SSE2/AVX blitting and blending paths, dirty-rect
  minimization, triple buffering only where measured to help. virtio-gpu acceleration later.
- **Toolkit:** immediate-mode-inspired retained toolkit written in **Tocin** (hosted mode)
  over a thin C ABI (`libtwin`), so apps get animations/theming without C++ template weight.

### 2.8 Performance & footprint budgets (enforced, not aspirational)
- Kernel image ≤ 4 MiB; boot-to-shell RSS ≤ 24 MiB; idle GUI desktop ≤ 96 MiB — each
  checked in CI once the corresponding milestone lands (budgets revisited with data).
- Zero-copy paths where they matter: page-cache-backed `read`/`write`, `sendfile`-style
  splice for the net stack, shared-memory window buffers for the GUI.
- Dead code is a footprint bug: every dormant subsystem from §1 gets wired or deleted —
  nothing ships compiled-but-unreachable.
- Benchmarks tracked per release vs. Alpine Linux on identical QEMU configs: boot time,
  syscall latency (getpid ping-pong), context-switch rate, FS throughput, frame times.
  "As efficient as Linux" is measured, not claimed; where we lose, the number and the
  reason go in the release notes.

### 2.9 Language strategy: C + C++ + Tocin
Assessment of the Tocin compiler (LLVM 18–22 backend, real AOT `.o`/ELF emission,
`--freestanding` mode, inline asm with constraints, volatile MMIO, C FFI — verified in
source Feb 2026):

- **C** — kernel core, boot, MM, interrupt paths. Unchanged.
- **C++ (freestanding subset)** — complex drivers and TocinFS v2 (`-fno-exceptions
  -fno-rtti`, no globals with ctors): RAII and templates earn their keep in B+tree and
  driver state machines.
- **Tocin (hosted)** — **first-class userspace language from M5:** system utilities,
  `mkfs.tocinfs`/`fsck`, the GUI toolkit and apps, services. It has GC, threads, channels,
  C FFI — a strong app language on top of our libc.
- **Tocin (freestanding)** — kernel *leaf modules only* for now (its freestanding mode is
  real but thin: int-addressed memory, no typed pointers, host-triple-only codegen).
  Upstream tocinlang work needed before deeper kernel use, filed as issues there:
  1. typed pointers + `repr(C)` struct layout (MMIO register structs)
  2. cross-compilation: `--target x86_64-unknown-none`, code model, red-zone control
  3. naked/interrupt function attributes + calling-convention control
  4. runtime-free aggregates in freestanding mode
  5. entry-point/linker-script ergonomics

---

## 3. Milestones

Each milestone has acceptance criteria; a milestone is closed only when its criteria pass
in CI. Order encodes hard dependencies.

### M0 — Truth and green (NOW)
- [x] Build fixed (`kernel/net` + `kernel/drivers/net` compile; duplicate TCP/IP stack
      unified at the IP layer; kernel links; QEMU boots)
- [ ] CI green on every push; boot smoke test with FAT disk attached
- [ ] Version truth: kernel banner, README, and `docs/` agree
- [ ] Dead-code triage: every §1 dormant item gets a milestone tag or is deleted
- [ ] Real unit tests: compile actual `pmm.c`/`vmm.c`/`scheduler.c` against a test harness
      (retire the mock-only suite)

### M1 — TocinBoot (custom bootloader)
- UEFI app boots the kernel on QEMU/OVMF with `tocinboot_info` handoff (memmap, GOP fb, RSDP)
- BIOS stage2 produces the same handoff struct
- Graphical splash; kernel renders to GOP framebuffer
- **Accept:** same kernel binary boots via BIOS and UEFI paths in CI

### M2 — Memory management, for real + 64-bit userspace
- Buddy allocator + slab live as *the* kernel allocators (delete bitmap-only path)
- Demand paging + `mmap` backed by page cache; copy-on-write `fork`
- x86-64: 4-level paging, `syscall`/`sysret`, 64-bit user programs
- **Accept:** stress test (fork storm + mmap churn) survives; RSS budget check added to CI

### M3 — Processes & threads that work
- `thread_clone` creates real scheduler-visible threads; TLS via GDT/FS-base; futex wired
- User pthreads: `threadtest.elf` runs concurrently (built and shipped on the disk image)
- Scheduler: decide CFS vs. current O(1)-style — benchmark, keep one, **delete the other**
- **Accept:** pthread test matrix (create/join/mutex/cond/TLS) green in QEMU CI

### M4 — Storage stack & TocinFS v2
- Block layer (`bio`), AHCI + NVMe + virtio-blk drivers
- `docs/TOCINFS_SPEC.md` frozen at v1; kernel read/write; `mkfs`/`fsck` in Tocin
- 1 PiB sparse-volume create/mount/verify test in CI; 16 TiB file seek/write/read test
- **Accept:** boot from TocinFS root on NVMe in QEMU

### M5 — Userspace platform: libc, ld.so, `.tox`/`.tap`
- Port a real libc (mlibc or musl-style) instead of growing `user/libc` ad hoc
- Dynamic linking end-to-end: `ld.so` on the system image, `libc.so` shared, symbol
  versioning minimal; `dynhello.elf` runs via `/LD.SO` in CI
- `.tox` note emitted by our toolchain wrapper; `.tap` packer + kernel loop-mount execution
- Tocin toolchain integration: `make apps` builds Tocin userspace programs into the image
- **Accept:** shell, coreutils-lite, and one Tocin GUI-less app ship as `.tap`

### M6 — Linux compatibility
- Stages 1–4 of §2.5 (static musl → busybox → dynamic → threaded)
- squashfs read-only driver + loop device; AppImage execution (stage 5)
- **Accept:** unmodified Alpine busybox and one real-world AppImage run in CI

### M7 — Graphics & desktop
- Framebuffer console (replaces VGA text as default), then TocinWM compositor
- `libtwin` C ABI + Tocin toolkit; terminal, launcher, file manager as first apps
- Animation: window open/close/move at 60 fps in QEMU (frame-time histogram in CI artifacts)
- **Accept:** desktop session with 3 concurrent animated apps ≤ 96 MiB total

### M8 — Networking live
- NIC drivers wired: RX interrupt → `tcpip_process_packet` → ARP/ICMP/TCP/UDP (stack
  already unified at the IP layer as of M0)
- Socket syscalls (`socket/bind/listen/accept/connect/send/recv`) + `select/poll`
- DHCP + DNS clients (Tocin, userspace); `ping` and an HTTP fetch in CI
- **Accept:** kernel serves a static page to the QEMU host; Linux-compat `wget` works

### Beyond (v3.x)
SMP scheduling, ACPI power management, audio, Wayland-protocol compat for TocinWM,
virtio-gpu 3D, package repository for `.tap`, self-hosting Tocin compiler on TocinOS.

---

## 4. Working agreements

1. **Spec before disk formats and ABIs** (`docs/TOCINFS_SPEC.md`, `docs/TOX_FORMAT.md`,
   `docs/BOOT_PROTOCOL.md` precede their implementations).
2. **Wired or deleted** — no new compiled-but-unreachable code; PRs adding a subsystem must
   also add its init call and a test that reaches it.
3. **Measure before replacing** — scheduler, allocators, and rendering paths are swapped
   only with benchmark evidence attached to the PR.
4. **CI is the referee** — every milestone's acceptance criteria are executable.
