# Makefile for TocinOS Shared C Library

CC = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -fno-pie -fno-stack-protector -fno-builtin -Wall -Wextra -O2 -g -fPIC

LDFLAGS = -m elf_i386 -shared -T libc.ld

TARGET = libc.so
OBJS = libc.o pthread.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

libc.o: libc.c
	$(CC) $(CFLAGS) -c $< -o $@

pthread.o: pthread.c pthread.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
