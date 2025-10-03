; TocinOS Stage 2 Bootloader
; This bootloader sets up protected mode and loads the kernel
; Supports both 32-bit and 64-bit kernel loading

[BITS 16]
[ORG 0x7E00]

stage2_start:
    ; Display Stage 2 message
    mov si, stage2_msg
    call print_string
    
    ; Enable A20 line for accessing memory above 1MB
    call enable_a20
    
    ; Load kernel from disk
    ; Kernel starts at sector 18 (after MBR + Stage2)
    mov ah, 0x02        ; BIOS read sectors function
    mov al, 0x20        ; Number of sectors to read (32 sectors = 16KB)
    mov ch, 0x00        ; Cylinder 0
    mov cl, 0x12        ; Sector 18
    mov dh, 0x00        ; Head 0
    mov bx, 0x1000      ; Load kernel at 0x10000 (64KB mark)
    mov es, bx
    xor bx, bx
    int 0x13
    
    jc kernel_load_error
    
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

; GDT for Protected Mode
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

[BITS 32]
protected_mode_start:
    ; Setup segments
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Setup stack
    mov esp, 0x90000
    
    ; Jump to kernel
    jmp 0x10000

enter_long_mode:
    ; Setup page tables and enter long mode
    ; This is simplified - real implementation needs proper page tables
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

times 8192-($-$$) db 0  ; Pad to 8KB
