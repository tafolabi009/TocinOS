# TocinBoot v0.1 — UEFI loader (standalone: `make -C boot/uefi`)
#
# Products (all under <repo>/build/uefi/):
#   TOCINBOOT.EFI   PE32+ EFI application (x86-64), built with MinGW-w64
#   esp/            staged ESP tree (EFI/BOOT/BOOTX64.EFI [+ KERNEL.ELF])
#   esp.img         16 MiB FAT boot image assembled with mtools (no root)
#
# Targets: all (default) | esp | img | test | clean
#          esp64 | img64 | test64  (M2: boots the ELF64 stub kernel —
#          build it first with `make kernel64-stub` at the repo root)

CROSS ?= x86_64-w64-mingw32-
CC    := $(CROSS)gcc
NASM  ?= nasm

TOP   := ../..
BUILD := $(TOP)/build/uefi
ESP   := $(BUILD)/esp
IMG   := $(BUILD)/esp.img
ESP64 := $(BUILD)/esp64
IMG64 := $(BUILD)/esp64.img

KERNEL_ELF := $(TOP)/build/kernel.elf
KERNEL64_STUB := $(TOP)/build/kernel64_stub.elf

# MinGW defaults to the MS ABI (== EFIAPI) and u"..." literals are CHAR16;
# no -fshort-wchar needed. Subsystem 10 = EFI application.
CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check \
          -mno-red-zone -Wall -Wextra -I$(TOP)/include -I$(BUILD) -I.
LDFLAGS := -nostdlib -Wl,--subsystem,10 -e efi_main

.PHONY: all esp img test esp64 img64 test64 clean

all: $(BUILD)/TOCINBOOT.EFI

$(BUILD):
	mkdir -p $@

$(BUILD)/handoff32.bin: handoff32.asm | $(BUILD)
	$(NASM) -f bin -o $@ $<

# Embed the trampoline blob as a C byte array (od+sed: no xxd dependency).
$(BUILD)/handoff32.h: $(BUILD)/handoff32.bin
	{ echo '/* generated from handoff32.bin — do not edit */'; \
	  echo 'static const unsigned char handoff32_blob[] = {'; \
	  od -An -v -tx1 $< | sed -e 's/[0-9a-f][0-9a-f]/0x&,/g'; \
	  echo '};'; } > $@

$(BUILD)/TOCINBOOT.EFI: tocinboot.c efi.h $(TOP)/include/boot/tocinboot.h \
                        $(BUILD)/handoff32.h | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tocinboot.c

# Staged ESP tree. KERNEL.ELF is optional: without it the image still boots
# and TocinBoot reports the missing kernel (build it with `make` at the root).
esp: all
	mkdir -p $(ESP)/EFI/BOOT $(ESP)/EFI/TOCINOS
	cp $(BUILD)/TOCINBOOT.EFI $(ESP)/EFI/BOOT/BOOTX64.EFI
	@if [ -f $(KERNEL_ELF) ]; then \
	    cp $(KERNEL_ELF) $(ESP)/EFI/TOCINOS/KERNEL.ELF; \
	    echo "esp: staged KERNEL.ELF"; \
	else \
	    rm -f $(ESP)/EFI/TOCINOS/KERNEL.ELF; \
	    echo "esp: $(KERNEL_ELF) not found — image will have no kernel"; \
	fi

img: esp
	rm -f $(IMG)
	dd if=/dev/zero of=$(IMG) bs=1M count=16 status=none
	mformat -i $(IMG) ::
	mcopy -i $(IMG) -s $(ESP)/EFI ::/
	@echo "img: $(IMG) ready"

test: img
	./test-ovmf.sh $(IMG)

# ---- M2 64-bit variant: stage the ELF64 stub kernel as KERNEL.ELF ---------
# Exercises the spec §6.2 long-mode handoff end-to-end under OVMF.

esp64: all
	mkdir -p $(ESP64)/EFI/BOOT $(ESP64)/EFI/TOCINOS
	cp $(BUILD)/TOCINBOOT.EFI $(ESP64)/EFI/BOOT/BOOTX64.EFI
	@if [ -f $(KERNEL64_STUB) ]; then \
	    cp $(KERNEL64_STUB) $(ESP64)/EFI/TOCINOS/KERNEL.ELF; \
	    echo "esp64: staged kernel64_stub.elf as KERNEL.ELF"; \
	else \
	    echo "esp64: $(KERNEL64_STUB) not found — run 'make kernel64-stub' at the repo root"; \
	    exit 1; \
	fi

img64: esp64
	rm -f $(IMG64)
	dd if=/dev/zero of=$(IMG64) bs=1M count=16 status=none
	mformat -i $(IMG64) ::
	mcopy -i $(IMG64) -s $(ESP64)/EFI ::/
	@echo "img64: $(IMG64) ready"

test64: img64
	KERNEL_BANNER='=== TocinOS 64-bit stub: tocinboot_info OK' \
	KERNEL_BANNER_DESC='64-bit long-mode handoff OK' \
	./test-ovmf.sh $(IMG64)

clean:
	rm -rf $(BUILD)
