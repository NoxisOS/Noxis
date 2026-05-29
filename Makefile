# Noxis OS Build System

SHELL     = cmd
CC        = i686-elf-gcc
LD        = i686-elf-ld
AS        = C:\Users\gabin\AppData\Local\bin\NASM\nasm
OBJCOPY   = i686-elf-objcopy
BUILD_DIR = build

CFLAGS    = -std=c11 -ffreestanding -nostdlib -nostdinc \
            -Wall -Wextra -Werror -Wno-unused-parameter \
            -m32 -march=i686 -mtune=generic \
            -fno-stack-protector -fno-exceptions \
            -fno-asynchronous-unwind-tables \
            -mno-mmx -mno-sse -mgeneral-regs-only \
            -I src
CFLAGS   += -O0 -g

LDFLAGS   = -T linker.ld -nostdlib -m elf_i386
ASFLAGS   = -f elf32
BOOTFLAGS = -f bin

KERNEL_C_OBJS   = build/kernel/early.o build/kernel/isr.o build/kernel/panic.o \
                  build/hal/gdt.o build/hal/idt.o build/hal/pic.o \
                  build/mm/pmm.o build/mm/vmm.o build/mm/heap.o \
                  build/drivers/pit.o \
                  build/proc/process.o build/proc/scheduler.o \
                  build/syscall/syscall.o

KERNEL_ASM_OBJS = build/asm/kernel_entry.o build/asm/ports.o \
                  build/asm/gdt_load.o build/asm/idt_load.o \
                  build/asm/isr_stubs.o build/asm/paging.o \
                  build/asm/tss_load.o build/asm/user_enter.o \
                  build/asm/syscall_stub.o build/asm/user_demo.o

KERNEL_OBJS = $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

KERNEL_ELF  = build/kernel.elf
KERNEL_BIN  = build/kernel.bin
MBR_BIN     = build/mbr.bin
LOADER_BIN  = build/loader.bin
DISK_IMG    = build/noxis.img

QEMU = "D:\Program Files\qemu\qemu-system-i386"

.PHONY: all clean run run-debug
all: $(DISK_IMG)

$(DISK_IMG): $(MBR_BIN) $(LOADER_BIN) $(KERNEL_BIN)
	@echo BUILD $(DISK_IMG)
	powershell -NoProfile -Command "$$img='$(DISK_IMG)'; $$bytes=2880*512; $$f=New-Object IO.FileStream($$img,'Create'); $$f.SetLength($$bytes); $$f.Close(); $$fs=New-Object IO.FileStream($$img,'Open'); $$fs.Position=0; $$mbr=[IO.File]::ReadAllBytes('$(MBR_BIN)'); $$fs.Write($$mbr,0,$$mbr.Length); $$fs.Position=512; $$ldr=[IO.File]::ReadAllBytes('$(LOADER_BIN)'); $$fs.Write($$ldr,0,$$ldr.Length); $$fs.Position=2560; $$krnl=[IO.File]::ReadAllBytes('$(KERNEL_BIN)'); $$fs.Write($$krnl,0,$$krnl.Length); $$fs.Close(); echo '  -> 1.44 MB floppy image ready'"

$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld
	@echo LD   $@
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo BIN  $@
	$(OBJCOPY) -O binary $< $@

$(MBR_BIN): src/boot/mbr.asm src/boot/defines.asm
	@if not exist build mkdir build
	@echo AS   $@
	$(AS) $(BOOTFLAGS) -isrc/boot/ $< -o $@

$(LOADER_BIN): src/boot/loader.asm src/boot/defines.asm
	@if not exist build mkdir build
	@echo AS   $@
	$(AS) $(BOOTFLAGS) -isrc/boot/ $< -o $@

build/kernel/%.o: src/kernel/%.c
	@if not exist build\kernel mkdir build\kernel
	@echo CC   $<
	$(CC) $(CFLAGS) -c $< -o $@

build/hal/%.o: src/hal/%.c
	@if not exist build\hal mkdir build\hal
	@echo CC   $<
	$(CC) $(CFLAGS) -c $< -o $@

build/mm/%.o: src/mm/%.c
	@if not exist build\mm mkdir build\mm
	@echo CC   $<
	$(CC) $(CFLAGS) -c $< -o $@

build/drivers/%.o: src/drivers/%.c
	@if not exist build\drivers mkdir build\drivers
	@echo CC   $<
	$(CC) $(CFLAGS) -c $< -o $@

build/proc/%.o: src/proc/%.c
	@if not exist build\proc mkdir build\proc
	@echo CC   $<
	$(CC) $(CFLAGS) -c $< -o $@

build/syscall/%.o: src/syscall/%.c
	@if not exist build\syscall mkdir build\syscall
	@echo CC   $<
	$(CC) $(CFLAGS) -c $< -o $@

build/asm/%.o: src/asm/%.asm
	@if not exist build\asm mkdir build\asm
	@echo AS   $<
	$(AS) $(ASFLAGS) $< -o $@

run: $(DISK_IMG)
	@echo RUN  QEMU
	@taskkill /F /IM qemu-system-i386.exe >nul 2>&1 || echo.
	$(QEMU) -fda $(DISK_IMG) -no-reboot -no-shutdown

run-debug: $(DISK_IMG)
	@taskkill /F /IM qemu-system-i386.exe >nul 2>&1 || echo.
	@echo RUN  QEMU + GDB on :1234
	start "QEMU-DEBUG" /B $(QEMU) -fda $(DISK_IMG) -no-reboot -no-shutdown -s -S
	@echo Connect: i686-elf-gdb -x .gdbinit

clean:
	@if exist build rmdir /s /q build
