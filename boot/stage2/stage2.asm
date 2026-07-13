; TocinOS Stage 2 Bootloader
; Sets up protected mode, loads the kernel, and enters it with the
; tocinboot_info v1 handoff (docs/BOOT_PROTOCOL.md) — the SAME contract the
; TocinBoot UEFI loader uses, so the kernel sees one entry contract.
;
; Physical memory owned by this stage2 (all inside the low USABLE region,
; which we re-type as BOOTLOADER in the handoff map, spec §4/§4.1):
;
;   0x07C00 - 0x07DFF   MBR (stage1), also real-mode stack top (grows down)
;   0x07E00 - 0x09DFF   this stage2 (16 sectors, loaded by the MBR)
;   0x10000 - 0x4FFFF   kernel staging buffer (KERNEL_MAX_SECTORS * 512),
;                       real-mode reachable; dead after the copy to 1 MiB
;   0x60000 - 0x600E7   tocinboot_info (4 KiB aligned, below 4 GiB, §3.2)
;   0x60800 - 0x60AFF   memory map array (up to 32 x 24-byte entries, §4)
;   0x90000             protected-mode handoff stack top (grows down;
;                       0x50000-0x8FFFF free, >16 KiB as §6 requires, and
;                       safely below the kernel VMM's page tables at
;                       0x9C000-0x9FFFF, see kernel/mm/vmm.c)
;
; NOT used: 0x70000 (the long-mode stub points CR3 there) and
; 0x9C000-0x9FFFF (kernel VMM page tables overwrite it).
;
; The kernel (raw kernel.bin, image sectors LBA 17+) is linked at 1 MiB
; (linker_x86.ld) with entry _start = 0x101000 (.multiboot occupies
; 0x100000-0x10000B, .text is ALIGN(4K) and entry.o is linked first).
; Real mode can't reach 1 MiB through BIOS reads, so we stage the image at
; 0x10000 and copy it up right after entering protected mode.

[BITS 16]
[ORG 0x7E00]

; ---- tocinboot_info v1 constants (docs/BOOT_PROTOCOL.md, tocinboot.h) ----
TBI_INFO_ADDR      equ 0x60000       ; 4 KiB aligned, below 4 GiB (§3.2)
TBI_MMAP_ADDR      equ 0x60800       ; 8-byte aligned memmap array (§3.2)
TBI_MMAP_MAX       equ 32            ; entry capacity (QEMU/SeaBIOS emits ~7)
TBI_INFO_MAGIC     equ 0x31494254    ; "TBI1"
TBI_REG_MAGIC      equ 0x70C1B007    ; EAX at entry (§6.1)
TBI_INFO_SIZE      equ 232
TBI_F_BIOS         equ (1 << 1)      ; flags: booted via the BIOS path (§3.1)

; Normalized memory types (§4.1)
MEM_USABLE         equ 1
MEM_RESERVED       equ 2
MEM_BOOTLOADER     equ 6
MEM_KERNEL_MODULES equ 7

; ---- kernel load parameters ----
KERNEL_LBA         equ 17            ; first kernel sector (root Makefile
                                     ; IMAGE rule: dd seek=17)
KERNEL_MAX_SECTORS equ 512           ; 256 KiB staging budget (kernel.bin is
                                     ; ~364 sectors today; IMAGE rule asserts
                                     ; it still fits)
KERNEL_STAGE_SEG   equ 0x1000        ; staging buffer at 0x10000
KERNEL_PHYS        equ 0x100000      ; link/run address (linker_x86.ld)
KERNEL_ENTRY       equ 0x101000      ; _start (see header comment)
; Conservative kernel extent for the handoff: kernel.bin gives no memsz, so
; we claim [1 MiB, 4 MiB) — covers .bss (ends ~0x3B6000 today) with headroom.
; Reported both as kernel_phys_base/end and as a KERNEL_MODULES map entry
; carved from the USABLE region (spec §4.2 allows either presentation).
KERNEL_RESERVE_END equ 0x400000
KRES_LEN           equ KERNEL_RESERVE_END - KERNEL_PHYS

; Fallback geometry (1.44 MB floppy). The real geometry is queried from the
; BIOS at load time (INT 13h AH=08h): QEMU/SeaBIOS serves this same image as
; an ~18/2 floppy under `-fda` but as a ~63/16 IDE disk under `-drive`, and
; hardcoding 18/2 mis-addresses every sector past track 0 in the latter mode
; (the original cause of the broken raw-image boot path).
FLOPPY_SPT         equ 18            ; sectors per track
FLOPPY_HEADS       equ 2

stage2_start:
    ; Save boot drive (passed from MBR in DL)
    mov [boot_drive], dl

    ; Display Stage 2 message
    mov si, stage2_msg
    call print_string

    ; Enable A20 line for accessing memory above 1MB
    call enable_a20

    ; Query the BIOS memory map while we still can (real mode only).
    ; Raw E820 entries land directly at TBI_MMAP_ADDR; they are normalized
    ; into tocinboot types later, in 32-bit protected mode (build_memmap).
    call do_e820

    ; Show boot menu
    call show_boot_menu

    ; Stage the raw kernel image (image sectors KERNEL_LBA..) at 0x10000.
    call load_kernel

    ; Check if we should boot into 64-bit mode
    call check_long_mode
    jc boot_32bit       ; If no 64-bit support, use 32-bit

    ; Enter 64-bit mode
    mov si, entering_64bit_msg
    call print_string
    jmp enter_long_mode

boot_32bit:
    ; Enter 32-bit protected mode
    mov si, entering_32bit_msg
    call print_string

    cli                 ; Disable interrupts
    lgdt [gdt_descriptor]

    ; Set PE (Protection Enable) bit in CR0
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to 32-bit code
    jmp 0x08:protected_mode_start

kernel_load_error:
    mov si, kernel_err_msg
    call print_string
    hlt

enable_a20:
    ; Fast A20 gate enable
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

; -----------------------------------------------------------------------------
; do_e820 — INT 15h AX=E820h memory map query (spec §4 producer, BIOS side)
;
; Stores raw 24-byte E820 entries at TBI_MMAP_ADDR, count in [mmap_count].
; The ACPI 3.0 "extended attributes" dword is pre-set to 1 before every call
; so BIOSes that only return 20 bytes still leave a "valid" entry behind
; (the dword is zeroed again during normalization — spec: reserved MUST be 0).
; Zero-length entries are dropped (spec §4: length never 0).
; Falls back to INT 15h AX=E801h (then AH=88h) and synthesizes entries when
; E820 is unsupported.
; -----------------------------------------------------------------------------
do_e820:
    push es
    mov ax, TBI_MMAP_ADDR >> 4
    mov es, ax
    xor di, di
    mov word [mmap_count], 0
    xor ebx, ebx                 ; continuation value, 0 = first call
.next:
    mov dword [es:di + 20], 1    ; ACPI attr quirk: pre-set "valid" bit
    mov eax, 0x0000E820
    mov edx, 0x534D4150          ; 'SMAP'
    mov ecx, 24
    int 0x15
    jc .done                     ; CF on first call = unsupported; later = end
    cmp eax, 0x534D4150
    jne .done                    ; not 'SMAP': unsupported / stop
    ; Drop zero-length entries (don't advance DI: next call overwrites)
    mov eax, [es:di + 8]
    or  eax, [es:di + 12]
    jz .no_store
    inc word [mmap_count]
    add di, 24
    cmp word [mmap_count], TBI_MMAP_MAX
    jae .done
.no_store:
    test ebx, ebx                ; EBX=0 after call: map complete
    jnz .next
.done:
    cmp word [mmap_count], 0
    jne .ret                     ; got at least one E820 entry: use E820 map
    ; ---- fallback: synthesize entries from E801/88h + INT 12h ----
    xor di, di
    int 0x12                     ; AX = KiB of low memory (typically 639)
    movzx eax, ax
    shl eax, 10                  ; -> bytes
    mov ecx, eax
    xor eax, eax
    call .emit                   ; {0, lowKiB, USABLE}
    xor cx, cx
    xor dx, dx
    mov ax, 0xE801
    int 0x15
    jc .try88
    test ax, ax                  ; some BIOSes report in CX/DX instead
    jnz .e801_ok
    mov ax, cx
    mov bx, dx
.e801_ok:
    movzx ecx, ax                ; AX = KiB between 1 MiB and 16 MiB
    shl ecx, 10
    mov eax, 0x100000
    call .emit                   ; {1 MiB, AX KiB, USABLE}
    test bx, bx                  ; BX = 64 KiB blocks above 16 MiB
    jz .ret
    movzx ecx, bx
    shl ecx, 16
    mov eax, 0x1000000
    call .emit                   ; {16 MiB, BX*64 KiB, USABLE}
    jmp .ret
.try88:
    mov ah, 0x88
    int 0x15                     ; AX = KiB above 1 MiB
    jc .ret
    test ax, ax
    jz .ret
    movzx ecx, ax
    shl ecx, 10
    mov eax, 0x100000
    call .emit                   ; {1 MiB, AX KiB, USABLE}
.ret:
    pop es
    ret
; .emit: append entry {base=EAX (hi 0), length=ECX (hi 0), USABLE} at ES:DI
.emit:
    mov [es:di], eax
    mov dword [es:di + 4], 0
    mov [es:di + 8], ecx
    mov dword [es:di + 12], 0
    mov dword [es:di + 16], MEM_USABLE
    mov dword [es:di + 20], 0
    add di, 24
    inc word [mmap_count]
    ret

; -----------------------------------------------------------------------------
; load_kernel — read KERNEL_MAX_SECTORS from image LBA KERNEL_LBA to the
; staging buffer at KERNEL_STAGE_SEG:0000 (0x10000).
;
; Generic CHS loop (replaces the old hand-unrolled per-track reads, which
; stopped at 145 sectors and silently truncated the kernel). Geometry comes
; from INT 13h AH=08h (see FLOPPY_SPT comment). Each INT 13h read is capped
; to (a) the end of the current track and (b) the next 64 KiB physical
; boundary (floppy DMA can't cross one; harmless for disks). 3 retries with
; controller reset per chunk.
; -----------------------------------------------------------------------------
load_kernel:
    push es
    ; Query the boot drive's CHS geometry (AH=08h clobbers ES:DI: it returns
    ; the floppy parameter table pointer). Fall back to 18/2 on failure.
    mov ah, 0x08
    mov dl, [boot_drive]
    xor di, di
    int 0x13
    jc .geo_default
    and cl, 0x3F                ; CL bits 5:0 = sectors per track
    jz .geo_default
    movzx ax, cl
    mov [disk_spt], ax
    movzx ax, dh                ; DH = max head index
    inc ax
    mov [disk_heads], ax
    jmp .geo_done
.geo_default:
    mov word [disk_spt], FLOPPY_SPT
    mov word [disk_heads], FLOPPY_HEADS
.geo_done:
    mov word [cur_lba], KERNEL_LBA
    mov word [sectors_left], KERNEL_MAX_SECTORS
    mov word [dest_seg], KERNEL_STAGE_SEG
.next_chunk:
    cmp word [sectors_left], 0
    je .done
    ; LBA -> CHS with the queried geometry
    mov ax, [cur_lba]
    xor dx, dx
    mov bx, [disk_spt]
    div bx                      ; AX = track index, DX = sector - 1
    inc dl
    mov [chs_sec], dl           ; sector: 1..SPT (SPT <= 63)
    xor dx, dx
    mov bx, [disk_heads]
    div bx                      ; AX = cylinder, DX = head
    mov [chs_cyl], al           ; cylinders stay tiny (<= 529/SPT/heads)
    mov [chs_head], dl
    ; chunk = min(sectors left in track, sectors_left, sectors to 64K bound)
    mov ax, [disk_spt]
    inc ax
    sub al, [chs_sec]           ; sectors remaining in this track (1..SPT)
    cmp ax, [sectors_left]
    jbe .cap1
    mov ax, [sectors_left]
.cap1:
    mov bx, [dest_seg]
    and bx, 0x0FFF              ; paragraphs into the current 64 KiB bank
    mov cx, 0x1000
    sub cx, bx
    shr cx, 5                   ; -> sectors until the 64 KiB boundary
    cmp ax, cx
    jbe .cap2
    mov ax, cx
.cap2:
    mov [chunk], al
    mov di, 3                   ; retries
.retry:
    mov ax, [dest_seg]
    mov es, ax
    xor bx, bx                  ; ES:BX = destination
    mov ah, 0x02                ; BIOS read sectors
    mov al, [chunk]
    mov ch, [chs_cyl]
    mov cl, [chs_sec]
    mov dh, [chs_head]
    mov dl, [boot_drive]
    int 0x13
    jnc .advance
    xor ah, ah                  ; reset disk system and retry
    mov dl, [boot_drive]
    int 0x13
    dec di
    jnz .retry
    pop es
    jmp kernel_load_error
.advance:
    movzx ax, byte [chunk]
    add [cur_lba], ax
    sub [sectors_left], ax
    shl ax, 5                   ; sectors -> paragraphs (512/16)
    add [dest_seg], ax
    jmp .next_chunk
.done:
    pop es
    ret

check_long_mode:
    ; Check if CPUID is available
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x200000
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    jz .no_long_mode

    ; Check if extended CPUID is available
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    ; Check if long mode is available
    mov eax, 0x80000001
    cpuid
    test edx, 0x20000000
    jz .no_long_mode
    clc
    ret

.no_long_mode:
    stc
    ret

print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

; Boot menu with timeout
show_boot_menu:
    ; Clear screen
    mov ax, 0x0003
    int 0x10

    ; Display TocinOS logo
    mov si, logo_line1
    call print_string
    mov si, logo_line2
    call print_string
    mov si, logo_line3
    call print_string
    mov si, logo_line4
    call print_string

    ; Display menu
    mov si, menu_msg
    call print_string

    ; Display options
    mov si, option1_msg
    call print_string
    mov si, option2_msg
    call print_string
    mov si, option3_msg
    call print_string

    ; Display timeout message
    mov si, timeout_msg
    call print_string

    ; Wait for timeout (5 seconds) or key press
    mov cx, 50          ; 50 iterations * 0.1s = 5 seconds
.wait_loop:
    ; Check for key press
    mov ah, 0x01        ; Check for keystroke
    int 0x16
    jnz .key_pressed    ; Jump if key is ready

    ; Short delay (~0.1 seconds)
    mov ah, 0x86        ; Wait function
    mov cx, 0x0001      ; High word
    mov dx, 0x86A0      ; Low word (100,000 microseconds = 0.1s)
    int 0x15

    loop .wait_loop

    ; Timeout - proceed with default boot
    jmp .boot_default

.key_pressed:
    ; Read the key
    mov ah, 0x00
    int 0x16

    ; Check which key was pressed
    cmp al, '1'
    je .boot_default
    cmp al, '2'
    je .boot_safe
    cmp al, '3'
    je .boot_recovery

    ; Invalid key, wait again
    jmp show_boot_menu

.boot_default:
    mov si, booting_msg
    call print_string
    ret

.boot_safe:
    mov si, safe_mode_msg
    call print_string
    ret

.boot_recovery:
    mov si, recovery_msg
    call print_string
    ; In recovery, we could load a different kernel or enter a minimal shell
    ; For now, just boot normally
    ret

; GDT for Protected Mode (loader-owned, lives in BOOTLOADER memory, spec §6)
gdt_start:
gdt_null:
    dq 0x0

gdt_code:
    dw 0xFFFF           ; Limit (low)
    dw 0x0              ; Base (low)
    db 0x0              ; Base (middle)
    db 10011010b        ; Access byte
    db 11001111b        ; Flags + Limit (high)
    db 0x0              ; Base (high)

gdt_data:
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10010010b
    db 11001111b
    db 0x0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; Messages
stage2_msg db 'Stage 2 Bootloader loaded', 0x0D, 0x0A, 0
entering_32bit_msg db 'Entering 32-bit mode...', 0x0D, 0x0A, 0
entering_64bit_msg db 'Entering 64-bit mode...', 0x0D, 0x0A, 0
kernel_err_msg db 'Kernel load error!', 0x0D, 0x0A, 0

; Boot drive (set at stage2 entry)
boot_drive db 0

; tocinboot_info loader name (spec §3: NUL-terminated, <= 32 bytes)
loader_name_str db 'TocinBoot BIOS stage2', 0
LOADER_NAME_LEN equ $ - loader_name_str

; Memory map entry count (filled by do_e820, adjusted by build_memmap)
mmap_count dw 0

; load_kernel state
disk_spt     dw FLOPPY_SPT     ; CHS geometry from INT 13h AH=08h
disk_heads   dw FLOPPY_HEADS
cur_lba      dw 0
sectors_left dw 0
dest_seg     dw 0
chunk        db 0
chs_cyl      db 0
chs_head     db 0
chs_sec      db 0

; Boot menu messages
logo_line1 db '  _____         _       ___  ____  ', 0x0D, 0x0A, 0
logo_line2 db ' |_   _|__   __(_) _ _ / _ \\/ ___| ', 0x0D, 0x0A, 0
logo_line3 db '   | | / _ \\ / _| || | | | |\\___ \\ ', 0x0D, 0x0A, 0
logo_line4 db '   |_| \\___/ \\__|_||_| |_| ||____/ ', 0x0D, 0x0A, 0x0D, 0x0A, 0

menu_msg db '=== TocinOS Boot Menu ===', 0x0D, 0x0A, 0x0D, 0x0A, 0
option1_msg db '  [1] Boot TocinOS (Default)', 0x0D, 0x0A, 0
option2_msg db '  [2] Boot in Safe Mode', 0x0D, 0x0A, 0
option3_msg db '  [3] Recovery Mode', 0x0D, 0x0A, 0x0D, 0x0A, 0
timeout_msg db 'Press a key within 5 seconds or default boot will start...', 0x0D, 0x0A, 0
booting_msg db 0x0D, 0x0A, 'Booting TocinOS...', 0x0D, 0x0A, 0
safe_mode_msg db 0x0D, 0x0A, 'Starting in Safe Mode...', 0x0D, 0x0A, 0
recovery_msg db 0x0D, 0x0A, 'Entering Recovery Mode...', 0x0D, 0x0A, 0

[BITS 32]
; -----------------------------------------------------------------------------
; 32-bit protected mode: finish loading, build the tocinboot_info handoff,
; and enter the kernel per spec §6.1.
; -----------------------------------------------------------------------------
protected_mode_start:
    ; Setup segments (flat 4 GiB data, base 0 — spec §6.1)
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Setup stack (16-byte aligned, >16 KiB free below — spec §6)
    mov esp, 0x90000
    cld                         ; DF=0 at entry (spec §6); rep below needs it

    ; Copy the staged kernel image from 0x10000 to its 1 MiB link address.
    ; The staging tail past the real kernel.bin holds zeros from the image,
    ; so early .bss (up to 0x140000) gets zeroed for free.
    mov esi, KERNEL_STAGE_SEG << 4
    mov edi, KERNEL_PHYS
    mov ecx, (KERNEL_MAX_SECTORS * 512) / 4
    rep movsd

    ; Turn the raw E820 buffer into a spec §4 memory map, then fill the
    ; tocinboot_info block. Done here (not in real mode) because the u64
    ; compares/moves are far simpler with flat 32-bit addressing.
    call build_memmap
    call build_info

    ; Register contract (§6.1), set as the very last thing before the jump:
    ; EAX = TOCINBOOT_REG_MAGIC, EBX = physical address of tocinboot_info.
    mov eax, TBI_REG_MAGIC
    mov ebx, TBI_INFO_ADDR
    jmp 0x08:KERNEL_ENTRY

; -----------------------------------------------------------------------------
; build_memmap — normalize the raw E820 buffer at TBI_MMAP_ADDR in place:
;   1. types: E820 1..5 -> tocinboot 1..5 (identical values), unknown/0 -> 2
;      (RESERVED); zero each entry's reserved dword (spec §4 table)
;   2. carve [1 MiB, KERNEL_RESERVE_END) out of its USABLE entry and append
;      a KERNEL_MODULES entry for it (spec §4.2 producer option)
;   3. USABLE below 1 MiB -> BOOTLOADER: everything this loader owns at
;      handoff (MBR, stage2+GDT, info block, memmap, handoff stack) lives in
;      low memory, and §3.2 requires the info block inside a BOOTLOADER
;      region. Re-typing all of low RAM is the conservative reading; the
;      kernel reclaims BOOTLOADER memory later (§4.2), so nothing is lost.
;   4. bubble-sort by ascending base (spec §4 guarantee)
;   5. coalesce adjacent same-type contiguous entries (spec §4 guarantee)
; -----------------------------------------------------------------------------
build_memmap:
    ; ---- 1. normalize types + zero reserved dwords ----
    movzx ecx, word [mmap_count]
    test ecx, ecx
    jz .done
    mov esi, TBI_MMAP_ADDR
.norm:
    mov eax, [esi + 16]
    cmp eax, 1
    jb .force_reserved
    cmp eax, 5
    jbe .type_ok
.force_reserved:
    mov dword [esi + 16], MEM_RESERVED
.type_ok:
    mov dword [esi + 20], 0
    add esi, 24
    loop .norm

    ; ---- 2. carve the kernel range as KERNEL_MODULES ----
    ; Every BIOS reports extended memory starting exactly at 1 MiB, so only
    ; the base == 0x100000 case is handled; if no such USABLE entry exists
    ; (or the array is full) we skip the carve — kernel_phys_base/end in the
    ; info block still describe the kernel range (spec §4.2).
    cmp word [mmap_count], TBI_MMAP_MAX
    jae .low_convert
    movzx ecx, word [mmap_count]
    mov esi, TBI_MMAP_ADDR
.find_kernel:
    cmp dword [esi], KERNEL_PHYS
    jne .fk_next
    cmp dword [esi + 4], 0
    jne .fk_next
    cmp dword [esi + 16], MEM_USABLE
    jne .fk_next
    mov eax, [esi + 12]          ; length must exceed the carve size
    test eax, eax
    jnz .carve
    cmp dword [esi + 8], KRES_LEN
    jbe .low_convert
.carve:
    add dword [esi], KRES_LEN    ; shrink the USABLE entry from the front
    adc dword [esi + 4], 0
    sub dword [esi + 8], KRES_LEN
    sbb dword [esi + 12], 0
    movzx eax, word [mmap_count] ; append {1 MiB, KRES_LEN, KERNEL_MODULES}
    lea eax, [eax + eax * 2]
    shl eax, 3                   ; * 24
    lea edi, [eax + TBI_MMAP_ADDR]
    mov dword [edi], KERNEL_PHYS
    mov dword [edi + 4], 0
    mov dword [edi + 8], KRES_LEN
    mov dword [edi + 12], 0
    mov dword [edi + 16], MEM_KERNEL_MODULES
    mov dword [edi + 20], 0
    inc word [mmap_count]
    jmp .low_convert
.fk_next:
    add esi, 24
    dec ecx
    jnz .find_kernel             ; (not LOOP: carve block exceeds short range)

    ; ---- 3. USABLE below 1 MiB -> BOOTLOADER ----
.low_convert:
    movzx ecx, word [mmap_count]
    mov esi, TBI_MMAP_ADDR
.low:
    cmp dword [esi + 16], MEM_USABLE
    jne .low_next
    cmp dword [esi + 4], 0
    jne .low_next
    cmp dword [esi], 0x100000
    jae .low_next
    mov dword [esi + 16], MEM_BOOTLOADER
.low_next:
    add esi, 24
    loop .low

    ; ---- 4. sort by ascending u64 base (bubble; N <= 33) ----
    movzx ebx, word [mmap_count]
    dec ebx
    jz .coalesce                 ; single entry: nothing to sort
.sort_pass:
    mov esi, TBI_MMAP_ADDR
    movzx ecx, word [mmap_count]
    dec ecx
.sort_pair:
    mov eax, [esi + 4]           ; compare bases: high dwords first
    cmp eax, [esi + 28]
    ja .swap
    jb .no_swap
    mov eax, [esi]
    cmp eax, [esi + 24]
    jbe .no_swap
.swap:
    push ecx
    mov ecx, 6                   ; 24 bytes = 6 dwords
    xor edx, edx
.swap_dword:
    mov eax, [esi + edx]
    mov ebp, [esi + edx + 24]
    mov [esi + edx], ebp
    mov [esi + edx + 24], eax
    add edx, 4
    dec ecx
    jnz .swap_dword
    pop ecx
.no_swap:
    add esi, 24
    loop .sort_pair
    dec ebx
    jnz .sort_pass

    ; ---- 5. coalesce adjacent same-type contiguous entries ----
.coalesce:
    mov esi, TBI_MMAP_ADDR       ; ESI -> entry i, EBX = i
    xor ebx, ebx
.co_loop:
    movzx ecx, word [mmap_count]
    lea eax, [ebx + 1]
    cmp eax, ecx
    jae .done
    mov eax, [esi + 16]          ; same type?
    cmp eax, [esi + 40]
    jne .co_next
    mov eax, [esi]               ; contiguous? base + length == next.base
    add eax, [esi + 8]
    mov edx, [esi + 4]
    adc edx, [esi + 12]
    cmp eax, [esi + 24]
    jne .co_next
    cmp edx, [esi + 28]
    jne .co_next
    mov eax, [esi + 32]          ; merge: length += next.length
    add [esi + 8], eax
    mov eax, [esi + 36]
    adc [esi + 12], eax
    movzx ecx, word [mmap_count] ; delete entry i+1: shift the tail down
    sub ecx, ebx
    sub ecx, 2
    jz .co_shrunk
    lea ecx, [ecx + ecx * 2]
    shl ecx, 1                   ; entries * 6 dwords
    push esi
    push edi
    lea edi, [esi + 24]
    lea esi, [esi + 48]
    rep movsd
    pop edi
    pop esi
.co_shrunk:
    dec word [mmap_count]
    jmp .co_loop                 ; recheck the same i against its new neighbor
.co_next:
    add esi, 24
    inc ebx
    jmp .co_loop
.done:
    ret

; -----------------------------------------------------------------------------
; build_info — fill the 232-byte tocinboot_info v1 block at TBI_INFO_ADDR.
; Offsets per docs/BOOT_PROTOCOL.md §3 / include/boot/tocinboot.h.
; Everything not set below stays zero: fb_* (fb_format=NONE — BIOS text
; mode, VBE out of scope), rsdp/cmdline/initrd (= none), reserved fields.
; -----------------------------------------------------------------------------
build_info:
    mov edi, TBI_INFO_ADDR
    xor eax, eax
    mov ecx, TBI_INFO_SIZE / 4
    rep stosd
    mov edi, TBI_INFO_ADDR
    mov dword [edi + 0x00], TBI_INFO_MAGIC   ; magic  "TBI1"
    mov dword [edi + 0x04], 1                ; version
    mov dword [edi + 0x08], TBI_INFO_SIZE    ; size = 232
    mov dword [edi + 0x0C], TBI_F_BIOS       ; flags: BIOS path, no fb
    mov dword [edi + 0x10], TBI_MMAP_ADDR    ; memmap_addr (u64, high = 0)
    movzx eax, word [mmap_count]
    mov [edi + 0x18], eax                    ; memmap_count
    mov dword [edi + 0x1C], 24               ; memmap_entry_size
    mov dword [edi + 0x70], KERNEL_PHYS      ; kernel_phys_base
    mov dword [edi + 0x78], KERNEL_RESERVE_END ; kernel_phys_end (see
                                             ; KERNEL_RESERVE_END comment)
    mov dword [edi + 0x80], KERNEL_ENTRY     ; kernel_entry
    mov esi, loader_name_str                 ; loader_name[32]
    lea edi, [edi + 0x88]
    mov ecx, LOADER_NAME_LEN
    rep movsb
    ret

enter_long_mode:
    ; Setup page tables and enter long mode
    ; TODO(M2): this path is non-functional scaffolding — nothing ever builds
    ; page tables at 0x70000, the GDT has no 64-bit (L=1) code segment, and
    ; the kernel is neither copied to 1 MiB nor entered per spec §6.2
    ; (RAX=magic, RDI=info). The tocinboot_info handoff for the 64-bit entry
    ; lands with the M2 ELF64 kernel; CI only exercises the 32-bit path.
    cli
    lgdt [gdt_descriptor]

    ; Enable PAE
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax

    ; Load page directory
    mov eax, 0x70000    ; Page tables at 0x70000
    mov cr3, eax

    ; Enable long mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    ; Jump to 64-bit kernel
    jmp 0x08:0x10000

times 8192-($-$$) db 0  ; Pad to 8KB (16 sectors: MBR load count + IMAGE rule)
