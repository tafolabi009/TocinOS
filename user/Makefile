# TocinOS User Programs Makefile

CC = gcc
CFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
         -nostartfiles -nodefaultlibs -Wall -Wextra -fno-pic -I.
LDFLAGS = -m32 -nostdlib -static -T linker.ld

PROGRAMS = hello.elf echo.elf cat.elf ls.elf shell.elf

.PHONY: all clean

all: $(PROGRAMS)

%.elf: %.c linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(PROGRAMS) *.o
