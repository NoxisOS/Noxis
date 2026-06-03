# ─────────────────────────────────────────────────────────────
# Noxis OS Build System
# ─────────────────────────────────────────────────────────────

ifeq ($(OS),Windows_NT)
    SHELL       = cmd
    KILL_QEMU   = @taskkill /F /IM qemu-system-i386.exe >nul 2>&1 || echo.
    MKDIRP      = if not exist $(subst /,\,$1) mkdir $(subst /,\,$1)
    MAKE_FLOPPY = powershell -NoProfile -ExecutionPolicy Bypass -File tools/windows/build_floppy.ps1 \
                      -Out $(DISK_IMG) -Mbr $(MBR_BIN) -Loader $(LOADER_BIN) -Kernel $(KERNEL_BIN)
    MAKE_NOXFS  = powershell -NoProfile -ExecutionPolicy Bypass -File tools/windows/build_disk.ps1
else
    SHELL       = /bin/sh
    KILL_QEMU   = @true
    MKDIRP      = mkdir -p $1
    MAKE_FLOPPY = tools/linux/build_floppy.sh $(DISK_IMG) $(MBR_BIN) $(LOADER_BIN) $(KERNEL_BIN)
    MAKE_NOXFS  = tools/linux/build_disk.sh build/disk.img \
                      ctest.elf:build/ctest.elf nsh.elf:build/nsh.elf
endif

CC        = i686-elf-gcc
LD        = i686-elf-ld
AS        = nasm
AR        = i686-elf-ar
OBJCOPY   = i686-elf-objcopy

# ── Kernel flags (no FPU, no SSE, strict freestanding) ───────
CFLAGS  = -std=c11 -ffreestanding -nostdlib -nostdinc \
          -Wall -Wextra -Werror -Wno-unused-parameter \
          -m32 -march=i686 -mtune=generic \
          -fno-stack-protector -fno-exceptions \
          -fno-asynchronous-unwind-tables \
          -mno-mmx -mno-sse -mgeneral-regs-only \
          -I src -O2

# ── Noxlib / userland C flags (x87 FPU allowed for ring-3) ───
#
# -fno-builtin  : prevent GCC from silently replacing printf/puts/memcpy etc.
#                 with its own inline or library versions — we own every call.
# -O1           : keep basic optimisations but avoid aggressive inlining that
#                 can leave &local_param pointing at a register slot.
NOXLIB_CFLAGS = -std=c11 -ffreestanding -nostdlib -nostdinc \
                -Wall -Wextra -Werror -Wno-unused-parameter \
                -m32 -march=i686 -mtune=generic \
                -fno-stack-protector -fno-exceptions \
                -fno-asynchronous-unwind-tables \
                -fno-builtin \
                -I src/noxlib/include -O1

LDFLAGS   = -T linker.ld -nostdlib -m elf_i386
ASFLAGS   = -f elf32
BOOTFLAGS = -f bin

# ── Kernel C objects ─────────────────────────────────────────
KERNEL_C_OBJS = \
  build/kernel/core/early.o         build/kernel/isr/isr.o        \
  build/kernel/core/panic.o         \
  build/kernel/hal/gdt.o            build/kernel/hal/idt.o        \
  build/kernel/hal/pic.o            build/kernel/hal/fpu.o        \
  build/mm/phys/pmm.o               build/mm/virt/vmm.o           \
  build/mm/virt/heap.o              build/mm/virt/pagefault.o     \
  build/mm/slab.o                   build/mm/arena.o              \
  build/drivers/pit.o               build/drivers/kbd.o           \
  build/drivers/keymap.o            \
  build/drivers/ata.o               build/drivers/vga.o           \
  build/drivers/block/block.o       build/drivers/serial.o        \
  build/drivers/tty/tty.o           \
  build/proc/process.o              build/proc/scheduler.o        \
  build/proc/elf.o                  build/proc/exec.o             \
  build/kernel/syscall/syscall.o    \
  build/kernel/syscall/sys_io.o     \
  build/kernel/syscall/sys_fd.o     \
  build/kernel/syscall/sys_fs.o     \
  build/kernel/syscall/sys_proc.o   \
  build/kernel/syscall/sys_signal.o \
  build/kernel/syscall/sys_misc.o   \
  build/fs/vfs/vfs.o                build/fs/vfs/ramfs.o          \
  build/fs/noxfs/noxfs.o            build/fs/noxfs/buffer.o       \
  build/fs/synfs/synfs.o            \
  build/fs/pipe/pipe.o

# ── Kernel ASM objects ───────────────────────────────────────
KERNEL_ASM_OBJS = \
  build/boot/kernel_entry.o         \
  build/kernel/hal/gdt_load.o       build/kernel/hal/idt_load.o   \
  build/kernel/hal/tss_load.o       build/kernel/hal/ports.o      \
  build/mm/virt/paging.o            \
  build/kernel/isr/isr_stubs.o      \
  build/proc/kjmp.o                 build/proc/kthread_switch.o   \
  build/proc/user_enter.o           \
  build/kernel/syscall/msr.o        build/kernel/syscall/syscall_stub.o \
  build/kernel/syscall/sysenter_stub.o

KERNEL_OBJS = $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

# ── Output files ─────────────────────────────────────────────
KERNEL_ELF = build/kernel.elf
KERNEL_BIN = build/kernel.bin
MBR_BIN    = build/mbr.bin
LOADER_BIN = build/loader.bin
DISK_IMG   = build/noxis.img

QEMU = "D:\Program Files\qemu\qemu-system-i386"

.PHONY: all clean run run-headless run-debug
all: $(DISK_IMG)

# ── Noxlib ────────────────────────────────────────────────────
NOXLIB_CRT  = build/noxlib/crt/crt0.o

NOXLIB_OBJS = build/noxlib/sys/syscall.o     \
              build/noxlib/sys/signal.o      \
              build/noxlib/string/string.o   \
              build/noxlib/stdlib/malloc.o   \
              build/noxlib/stdlib/stdlib.o   \
              build/noxlib/stdio/stdio.o

build/noxlib.a: $(NOXLIB_OBJS)
	@echo AR   $@
	$(AR) rcs $@ $(NOXLIB_OBJS)

# Compile noxlib C files with NOXLIB_CFLAGS (not kernel CFLAGS)
build/noxlib/%.o: src/noxlib/%.c
	@$(call MKDIRP,$(@D))
	@echo CC   $<
	$(CC) $(NOXLIB_CFLAGS) $(DEPFLAGS) -c $< -o $@

# Assemble noxlib ASM files
build/noxlib/%.o: src/noxlib/%.asm
	@$(call MKDIRP,$(@D))
	@echo AS   $<
	$(AS) $(ASFLAGS) $< -o $@

# ── Userland ELFs ─────────────────────────────────────────────
USER_LD   = src/userland/user.ld

# C programs (crt0 + prog.o + noxlib.a)
C_ELFS    = build/ctest.elf build/nsh.elf

USER_ELFS = $(C_ELFS)

# ── C userland compilation (NOXLIB_CFLAGS) ────────────────────
build/ctest.o: src/userland/ctest.c
	@$(call MKDIRP,build)
	@echo CC   $<
	$(CC) $(NOXLIB_CFLAGS) $(DEPFLAGS) -c $< -o $@

# ── nsh shell (split into modules under src/userland/nsh/) ────
NSH_SRCS = $(wildcard src/userland/nsh/*.c)
NSH_OBJS = $(patsubst src/userland/nsh/%.c,build/nsh/%.o,$(NSH_SRCS))

build/nsh/%.o: src/userland/nsh/%.c
	@$(call MKDIRP,build/nsh)
	@echo CC   $<
	$(CC) $(NOXLIB_CFLAGS) $(DEPFLAGS) -c $< -o $@

# ── C ELF link: crt0 + prog.o + noxlib.a ─────────────────────
build/ctest.elf: $(NOXLIB_CRT) build/ctest.o build/noxlib.a $(USER_LD)
	@echo LD   $@
	$(LD) -T $(USER_LD) -nostdlib -m elf_i386 -o $@ \
	    $(NOXLIB_CRT) build/ctest.o build/noxlib.a

build/nsh.elf: $(NOXLIB_CRT) $(NSH_OBJS) build/noxlib.a $(USER_LD)
	@echo LD   $@
	$(LD) -T $(USER_LD) -nostdlib -m elf_i386 -o $@ \
	    $(NOXLIB_CRT) $(NSH_OBJS) build/noxlib.a

# ── Disk image ───────────────────────────────────────────────
# Disk-builder scripts are dependencies so editing them forces a rebuild.
DISK_TOOLS = tools/windows/build_floppy.ps1 tools/windows/build_disk.ps1 \
             tools/linux/build_floppy.sh    tools/linux/build_disk.sh

$(DISK_IMG): $(MBR_BIN) $(LOADER_BIN) $(KERNEL_BIN) $(USER_ELFS) $(DISK_TOOLS)
	$(KILL_QEMU)
	@echo BUILD $(DISK_IMG)
	$(MAKE_FLOPPY)
	@echo BUILD build/disk.img
	$(MAKE_NOXFS)

# ── Kernel link + strip ──────────────────────────────────────
$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld
	@echo LD   $@
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo BIN  $@
	$(OBJCOPY) -O binary $< $@

# ── Boot binaries ────────────────────────────────────────────
$(MBR_BIN): src/boot/mbr.asm src/boot/defines.asm
	@$(call MKDIRP,build)
	@echo AS   $@
	$(AS) $(BOOTFLAGS) -isrc/boot/ $< -o $@

$(LOADER_BIN): src/boot/loader.asm src/boot/defines.asm
	@$(call MKDIRP,build)
	@echo AS   $@
	$(AS) $(BOOTFLAGS) -isrc/boot/ $< -o $@

# ── Automatic header dependency tracking ─────────────────────
# -MMD  : write a .d file alongside each .o (lists header deps)
# -MP   : add phony targets for headers (avoids errors on header deletion)
# Both the kernel rule and the noxlib rule get DEPFLAGS so that changing
# any included header (e.g. common/signal.h) triggers correct recompilation.
DEPFLAGS = -MMD -MP

# ── Generic compile rules (match any depth) ───────────────────
build/%.o: src/%.c
	@$(call MKDIRP,$(@D))
	@echo CC   $<
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

build/%.o: src/%.asm
	@$(call MKDIRP,$(@D))
	@echo AS   $<
	$(AS) $(ASFLAGS) $< -o $@

# Include generated dependency files (silently skip if build/ doesn't exist yet)
-include $(wildcard build/*.d build/**/*.d build/**/**/*.d build/**/**/**/*.d)

# ── Run ──────────────────────────────────────────────────────
# VGA window + serial console (COM1) mirrored to this terminal.
run: $(DISK_IMG)
	@echo RUN  QEMU
	@taskkill /F /IM qemu-system-i386.exe >nul 2>&1 || echo.
	$(QEMU) -fda $(DISK_IMG) -drive file=build/disk.img,format=raw,if=ide,index=0 -no-reboot -serial stdio

# No VGA window — everything (boot log + init shell) on the serial console.
run-headless: $(DISK_IMG)
	@echo RUN  QEMU headless
	@taskkill /F /IM qemu-system-i386.exe >nul 2>&1 || echo.
	$(QEMU) -fda $(DISK_IMG) -drive file=build/disk.img,format=raw,if=ide,index=0 -no-reboot -display none -serial stdio

run-debug: $(DISK_IMG)
	@taskkill /F /IM qemu-system-i386.exe >nul 2>&1 || echo.
	@echo RUN  QEMU + GDB on :1234
	start "QEMU-DEBUG" /B $(QEMU) -fda $(DISK_IMG) -hda build/disk.img -no-reboot -no-shutdown -s -S
	@echo Connect: i686-elf-gdb -x .gdbinit

# ─────────────────────────────────────────────────────────────
# x86_64 port (work in progress, branch: x86_64)
# Milestone 2: 64-bit C kernel booted by a long-mode boot sector.
# ─────────────────────────────────────────────────────────────
QEMU64    = "D:\Program Files\qemu\qemu-system-x86_64"
TOOLS64   = D:/Program Files/x86_64-elf-tools-windows/bin
CC64      = "$(TOOLS64)/x86_64-elf-gcc"
LD64      = "$(TOOLS64)/x86_64-elf-ld"
OBJCOPY64 = "$(TOOLS64)/x86_64-elf-objcopy"

CFLAGS64  = -ffreestanding -nostdlib -nostdinc -m64 \
            -mno-red-zone -mno-mmx -mno-sse -mgeneral-regs-only \
            -fno-pic -fno-stack-protector -Wall -Wextra -O2

build/boot64_mbr.bin: src/boot64/boot.asm
	@$(call MKDIRP,build)
	$(AS) -f bin $< -o $@

build/b64_entry.o: src/boot64/entry.asm
	@$(call MKDIRP,build)
	$(AS) -f elf64 $< -o $@

build/b64_kmain.o: src/boot64/kmain.c
	@$(call MKDIRP,build)
	$(CC64) $(CFLAGS64) -c $< -o $@

build/b64_kernel.bin: build/b64_entry.o build/b64_kmain.o src/boot64/kernel.ld
	$(LD64) -T src/boot64/kernel.ld -nostdlib -o build/b64_kernel.elf \
	    build/b64_entry.o build/b64_kmain.o
	$(OBJCOPY64) -O binary build/b64_kernel.elf $@

build/boot64.img: build/boot64_mbr.bin build/b64_kernel.bin
	@echo BUILD $@
	powershell -NoProfile -ExecutionPolicy Bypass -Command "$$m=[IO.File]::ReadAllBytes('build/boot64_mbr.bin'); $$k=[IO.File]::ReadAllBytes('build/b64_kernel.bin'); $$img=New-Object byte[] (512*65); [Array]::Copy($$m,0,$$img,0,512); [System.Buffer]::BlockCopy($$k,0,$$img,512,$$k.Length); [IO.File]::WriteAllBytes('build/boot64.img',$$img)"

.PHONY: boot64 run64
boot64: build/boot64.img

run64: build/boot64.img
	@echo RUN  QEMU x86_64 (long mode)
	$(QEMU64) -drive file=build/boot64.img,format=raw -no-reboot

clean:
	@if exist build rmdir /s /q build
