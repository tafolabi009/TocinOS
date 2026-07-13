# TocinBoot v0.1 — UEFI loader (standalone: `make -C boot/uefi`)
#
# Products (all under <repo>/build/uefi/):
#   TOCINBOOT.EFI   PE32+ EFI application (x86-64), built with MinGW-w64
#   esp/            staged ESP tree (EFI/BOOT/BOOTX64.EFI [+ KERNEL.ELF])
#   esp.img         16 MiB FAT boot image assembled with mtools (no root)
#
# Targets: all (default) | esp | img | test | clean

CROSS ?= x86_64-w64-mingw32-
CC    := $(CROSS)gcc
NASM  ?= nasm

TOP   := ../..
BUILD := $(TOP)/build/uefi
ESP   := $(BUILD)/esp
IMG   := $(BUILD)/esp.img

KERNEL_ELF := $(TOP)/build/kernel.elf

# MinGW defaults to the MS ABI (== EFIAPI) and u"..." literals are CHAR16;
# no -fshort-wchar needed. Subsystem 10 = EFI application.
CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fno-stack-check \
          -mno-red-zone -Wall -Wextra -I$(TOP)/include -I$(BUILD) -I.
LDFLAGS := -nostdlib -Wl,--subsystem,10 -e efi_main

.PHONY: all esp img test clean

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

clean:
	rm -rf $(BUILD)
