; TocinOS M2 bring-up: minimal 64-bit stub kernel entry.
;
; TocinBoot enters here by `jmp` (never `call`) in 64-bit long mode with
; RAX = TOCINBOOT_REG_MAGIC and RDI = physical address of tocinboot_info
; (docs/BOOT_PROTOCOL.md §6.2). Loader page tables identity-map every
; memory-map region, so all boot-info pointers are directly dereferenceable.
;
; This entry hands both registers to C as SysV arguments — RDI is already
; the first argument (info pointer), the magic moves to RSI — on a private
; 16 KiB stack. stub64_main() prints the proof banner on COM1 and returns;
; the CPU is then parked for good.
;
; This file is the entry for the standalone kernel64-stub proof binary
; (root Makefile target `kernel64-stub`, linker_x86_64_stub.ld); the full
; M2 kernel keeps its own entry in entry.asm.

[BITS 64]
[EXTERN stub64_main]
[GLOBAL _start]

section .text
_start:
    mov rsi, rax                  ; arg1 = register magic (RDI = arg0 = info)
    lea rsp, [rel stub64_stack_top]
    and rsp, -16                  ; SysV: RSP 16-byte aligned before call
    cld
    call stub64_main

    ; stub64_main returned: park the CPU cleanly.
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stub64_stack_bottom:
    resb 16384                    ; 16 KiB stub stack
stub64_stack_top:

; Mark the stack non-executable (silences the GNU ld warning; the section
; itself is discarded by linker_x86_64_stub.ld).
section .note.GNU-stack noalloc noexec nowrite progbits
