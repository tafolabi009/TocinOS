; TocinOS Master Boot Record (MBR) - Stage 1 Bootloader
; This is the first stage bootloader that loads Stage 2
; Size: 512 bytes (Boot sector)

[BITS 16]
[ORG 0x7C00]

start:
    ; Initialize segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    
    ; Display boot message
    mov si, boot_msg
    call print_string
    
    ; Load Stage 2 bootloader from disk
    ; Stage 2 starts at sector 2 (after MBR)
    mov ah, 0x02        ; BIOS read sectors function
    mov al, 0x10        ; Number of sectors to read (16 sectors = 8KB)
    mov ch, 0x00        ; Cylinder 0
    mov cl, 0x02        ; Sector 2 (after MBR)
    mov dh, 0x00        ; Head 0
    mov bx, 0x7E00      ; Load Stage 2 at 0x7E00
    int 0x13            ; BIOS disk interrupt
    
    jc disk_error       ; Jump if carry flag set (error)
    
    ; Jump to Stage 2
    jmp 0x0000:0x7E00
    
disk_error:
    mov si, disk_err_msg
    call print_string
    hlt

print_string:
    lodsb               ; Load byte from SI into AL
    or al, al           ; Check if AL is zero
    jz .done
    mov ah, 0x0E        ; BIOS teletype output
    int 0x10            ; BIOS video interrupt
    jmp print_string
.done:
    ret

boot_msg db 'TocinOS MBR Loading...', 0x0D, 0x0A, 0
disk_err_msg db 'Disk read error!', 0x0D, 0x0A, 0

; Fill remaining space and add boot signature
times 510-($-$$) db 0
dw 0xAA55           ; Boot signature
