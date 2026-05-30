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
CFLAGS   += -O2

LDFLAGS   = -T linker.ld -nostdlib -m elf_i386
ASFLAGS   = -f elf32
BOOTFLAGS = -f bin

KERNEL_C_OBJS   = build/kernel/early.o build/kernel/isr.o build/kernel/panic.o \
                  build/hal/gdt.o build/hal/idt.o build/hal/pic.o \
                  build/mm/pmm.o build/mm/vmm.o build/mm/heap.o \
                  build/drivers/pit.o \
                  build/drivers/kbd.o \
                  build/drivers/ata.o \
                  build/drivers/vga.o \
                  build/proc/process.o build/proc/scheduler.o \
                  build/syscall/syscall.o \
                  build/fs/vfs.o build/fs/ramfs.o build/fs/noxfs.o \
                  build/proc/elf.o build/proc/exec.o \
                  build/shell/shell.o \
                  build/shell/cmd_help.o   build/shell/cmd_uptime.o \
                  build/shell/cmd_ls.o     build/shell/cmd_cat.o \
                  build/shell/cmd_exec.o   build/shell/cmd_clear.o \
                  build/shell/cmd_halt.o

KERNEL_ASM_OBJS = build/asm/kernel_entry.o build/asm/ports.o \
                  build/asm/gdt_load.o build/asm/idt_load.o \
                  build/asm/isr_stubs.o build/asm/paging.o \
                  build/asm/tss_load.o \
                  build/asm/syscall_stub.o \
                  build/asm/msr.o build/asm/sysenter_stub.o \
                  build/asm/user_enter.o \
                  build/asm/kjmp.o

KERNEL_OBJS = $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

KERNEL_ELF  = build/kernel.elf
KERNEL_BIN  = build/kernel.bin
MBR_BIN     = build/mbr.bin
LOADER_BIN  = build/loader.bin
DISK_IMG    = build/noxis.img

QEMU = "D:\Program Files\qemu\qemu-system-i386"

.PHONY: all clean run run-debug
all: $(DISK_IMG)

ROOTFS_FILES  = rootfs/motd rootfs/version rootfs/readme
USER_ELFS     = build/hello.elf build/echo.elf build/prompt.elf

# ── userland ELFs ──────────────────────────────────────────
USER_LD = src/userland/user.ld

build/hello.o: src/userland/hello.asm
	@if not exist build mkdir build
	@echo AS   $<
	$(AS) -f elf32 $< -o $@

build/hello.elf: build/hello.o $(USER_LD)
	@echo LD   $@
	$(LD) -T $(USER_LD) -nostdlib -m elf_i386 -o $@ build/hello.o

build/echo.o: src/userland/echo.asm
	@if not exist build mkdir build
	@echo AS   $<
	$(AS) -f elf32 $< -o $@

build/echo.elf: build/echo.o $(USER_LD)
	@echo LD   $@
	$(LD) -T $(USER_LD) -nostdlib -m elf_i386 -o $@ build/echo.o

build/prompt.o: src/userland/prompt.asm
	@if not exist build mkdir build
	@echo AS   $<
	$(AS) -f elf32 $< -o $@

build/prompt.elf: build/prompt.o $(USER_LD)
	@echo LD   $@
	$(LD) -T $(USER_LD) -nostdlib -m elf_i386 -o $@ build/prompt.o

$(DISK_IMG): $(MBR_BIN) $(LOADER_BIN) $(KERNEL_BIN) $(ROOTFS_FILES) $(USER_ELFS) tools/build_disk.ps1
	@taskkill /F /IM qemu-system-i386.exe >nul 2>&1 || echo.
	@echo BUILD $(DISK_IMG)
	powershell -NoProfile -Command "$$bytes=1474560; $$f=New-Object IO.FileStream('$(DISK_IMG)','Create'); $$f.SetLength($$bytes); $$f.Dispose(); $$mbr=[IO.File]::ReadAllBytes('$(MBR_BIN)'); $$ldr=[IO.File]::ReadAllBytes('$(LOADER_BIN)'); $$krnl=[IO.File]::ReadAllBytes('$(KERNEL_BIN)'); $$fs=New-Object IO.FileStream('$(DISK_IMG)','Open'); $$fs.Write($$mbr,0,$$mbr.Length); $$fs.Position=512; $$fs.Write($$ldr,0,$$ldr.Length); $$fs.Position=2560; $$fs.Write($$krnl,0,$$krnl.Length); $$fs.Dispose(); echo '  -> floppy'"
	@echo BUILD build/disk.img
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_disk.ps1

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

build/fs/%.o: src/fs/%.c
	@if not exist build\fs mkdir build\fs
	@echo CC   $<
	$(CC) $(CFLAGS) -c $< -o $@

build/shell/%.o: src/shell/%.c
	@if not exist build\shell mkdir build\shell
	@echo CC   $<
	$(CC) $(CFLAGS) -c $< -o $@

build/asm/%.o: src/asm/%.asm
	@if not exist build\asm mkdir build\asm
	@echo AS   $<
	$(AS) $(ASFLAGS) $< -o $@

run: $(DISK_IMG)
	@echo RUN  QEMU
	@taskkill /F /IM qemu-system-i386.exe >nul 2>&1 || echo.
	$(QEMU) -fda $(DISK_IMG) -hda build/disk.img -no-reboot -no-shutdown

run-debug: $(DISK_IMG)
	@taskkill /F /IM qemu-system-i386.exe >nul 2>&1 || echo.
	@echo RUN  QEMU + GDB on :1234
	start "QEMU-DEBUG" /B $(QEMU) -fda $(DISK_IMG) -hda build/disk.img -no-reboot -no-shutdown -s -S
	@echo Connect: i686-elf-gdb -x .gdbinit

clean:
	@if exist build rmdir /s /q build
