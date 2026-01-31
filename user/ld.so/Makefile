# Makefile for TocinOS Dynamic Linker (ld.so)

CC = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -fno-pie -fno-stack-protector -fno-builtin -Wall -Wextra -O2 -g

LDFLAGS = -m elf_i386 -T linker.ld

TARGET = ld.so

all: $(TARGET)

$(TARGET): ld.o
	$(LD) $(LDFLAGS) -o $@ $^

ld.o: ld.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f ld.o $(TARGET)

.PHONY: all clean
