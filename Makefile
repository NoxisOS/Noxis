# ─────────────────────────────────────────────────────────────
# Noxis OS Build System
# ─────────────────────────────────────────────────────────────

SHELL     = cmd
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
  build/drivers/pit.o               build/drivers/kbd.o           \
  build/drivers/ata.o               build/drivers/vga.o           \
  build/drivers/block/block.o       build/drivers/serial.o        \
  build/drivers/tty/tty.o           \
  build/proc/process.o              build/proc/scheduler.o        \
  build/proc/elf.o                  build/proc/exec.o             \
  build/kernel/syscall/syscall.o    \
  build/fs/vfs/vfs.o                build/fs/vfs/ramfs.o          \
  build/fs/noxfs/noxfs.o            build/fs/noxfs/buffer.o       \
  build/fs/pipe/pipe.o              \
  build/shell/shell.o               \
  build/shell/commands/cmd_help.o   build/shell/commands/cmd_uptime.o \
  build/shell/commands/cmd_ls.o     build/shell/commands/cmd_cat.o    \
  build/shell/commands/cmd_exec.o   build/shell/commands/cmd_clear.o  \
  build/shell/commands/cmd_halt.o   build/shell/commands/cmd_sleep.o \
  build/shell/commands/cmd_cd.o     build/shell/commands/cmd_mkdir.o \
  build/shell/commands/cmd_blkstat.o

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
	@if not exist $(subst /,\,$(@D)) mkdir $(subst /,\,$(@D))
	@echo CC   $<
	$(CC) $(NOXLIB_CFLAGS) $(DEPFLAGS) -c $< -o $@

# Assemble noxlib ASM files
build/noxlib/%.o: src/noxlib/%.asm
	@if not exist $(subst /,\,$(@D)) mkdir $(subst /,\,$(@D))
	@echo AS   $<
	$(AS) $(ASFLAGS) $< -o $@

# ── Userland ELFs ─────────────────────────────────────────────
USER_LD   = src/userland/user.ld

# ASM-only programs (single object, no noxlib)
ASM_ELFS  = build/hello.elf  build/echo.elf   build/prompt.elf \
            build/fread.elf  build/fork.elf   build/write.elf  \
            build/pipe.elf   build/signal.elf \
            build/ttytest.elf build/pftest.elf build/segv.elf \
            build/init.elf   build/brktest.elf build/fputest.elf \
            build/systest.elf build/loop.elf

# C programs (crt0 + prog.o + noxlib.a)
C_ELFS    = build/ctest.elf build/nsh.elf

USER_ELFS = $(ASM_ELFS) $(C_ELFS)

build/hello.o:   src/userland/hello.asm   ; $(AS) $(ASFLAGS) $< -o $@
build/echo.o:    src/userland/echo.asm    ; $(AS) $(ASFLAGS) $< -o $@
build/prompt.o:  src/userland/prompt.asm  ; $(AS) $(ASFLAGS) $< -o $@
build/fread.o:   src/userland/fread.asm   ; $(AS) $(ASFLAGS) $< -o $@
build/fork.o:    src/userland/fork.asm    ; $(AS) $(ASFLAGS) $< -o $@
build/write.o:   src/userland/write.asm   ; $(AS) $(ASFLAGS) $< -o $@
build/pipe.o:    src/userland/pipe.asm    ; $(AS) $(ASFLAGS) $< -o $@
build/signal.o:  src/userland/signal.asm  ; $(AS) $(ASFLAGS) $< -o $@
build/ttytest.o: src/userland/ttytest.asm ; $(AS) $(ASFLAGS) $< -o $@
build/pftest.o:  src/userland/pftest.asm  ; $(AS) $(ASFLAGS) $< -o $@
build/segv.o:    src/userland/segv.asm    ; $(AS) $(ASFLAGS) $< -o $@
build/init.o:    src/userland/init.asm    ; $(AS) $(ASFLAGS) $< -o $@
build/brktest.o: src/userland/brktest.asm ; $(AS) $(ASFLAGS) $< -o $@
build/fputest.o: src/userland/fputest.asm ; $(AS) $(ASFLAGS) $< -o $@
build/systest.o: src/userland/systest.asm ; $(AS) $(ASFLAGS) $< -o $@
build/loop.o:    src/userland/loop.asm    ; $(AS) $(ASFLAGS) $< -o $@

# Rule for ASM-only ELFs (single object, no noxlib)
$(ASM_ELFS): build/%.elf: build/%.o $(USER_LD)
	@echo LD   $@
	$(LD) -T $(USER_LD) -nostdlib -m elf_i386 -o $@ $<

# ── C userland compilation (NOXLIB_CFLAGS) ────────────────────
build/ctest.o: src/userland/ctest.c
	@if not exist build mkdir build
	@echo CC   $<
	$(CC) $(NOXLIB_CFLAGS) $(DEPFLAGS) -c $< -o $@

build/nsh.o: src/userland/nsh.c
	@if not exist build mkdir build
	@echo CC   $<
	$(CC) $(NOXLIB_CFLAGS) $(DEPFLAGS) -c $< -o $@

# ── C ELF link: crt0 + prog.o + noxlib.a ─────────────────────
build/ctest.elf: $(NOXLIB_CRT) build/ctest.o build/noxlib.a $(USER_LD)
	@echo LD   $@
	$(LD) -T $(USER_LD) -nostdlib -m elf_i386 -o $@ \
	    $(NOXLIB_CRT) build/ctest.o build/noxlib.a

build/nsh.elf: $(NOXLIB_CRT) build/nsh.o build/noxlib.a $(USER_LD)
	@echo LD   $@
	$(LD) -T $(USER_LD) -nostdlib -m elf_i386 -o $@ \
	    $(NOXLIB_CRT) build/nsh.o build/noxlib.a

# ── Disk image ───────────────────────────────────────────────
ROOTFS_FILES = rootfs/motd rootfs/version rootfs/readme

$(DISK_IMG): $(MBR_BIN) $(LOADER_BIN) $(KERNEL_BIN) $(ROOTFS_FILES) $(USER_ELFS) tools/build_disk.ps1
	@taskkill /F /IM qemu-system-i386.exe >nul 2>&1 || echo.
	@echo BUILD $(DISK_IMG)
	powershell -NoProfile -Command "$$bytes=1474560; $$f=New-Object IO.FileStream('$(DISK_IMG)','Create'); $$f.SetLength($$bytes); $$f.Dispose(); $$mbr=[IO.File]::ReadAllBytes('$(MBR_BIN)'); $$ldr=[IO.File]::ReadAllBytes('$(LOADER_BIN)'); $$krnl=[IO.File]::ReadAllBytes('$(KERNEL_BIN)'); $$fs=New-Object IO.FileStream('$(DISK_IMG)','Open'); $$fs.Write($$mbr,0,$$mbr.Length); $$fs.Position=512; $$fs.Write($$ldr,0,$$ldr.Length); $$fs.Position=2560; $$fs.Write($$krnl,0,$$krnl.Length); $$fs.Dispose(); echo '  -> floppy'"
	@echo BUILD build/disk.img
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_disk.ps1

# ── Kernel link + strip ──────────────────────────────────────
$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld
	@echo LD   $@
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo BIN  $@
	$(OBJCOPY) -O binary $< $@

# ── Boot binaries ────────────────────────────────────────────
$(MBR_BIN): src/boot/mbr.asm src/boot/defines.asm
	@if not exist build mkdir build
	@echo AS   $@
	$(AS) $(BOOTFLAGS) -isrc/boot/ $< -o $@

$(LOADER_BIN): src/boot/loader.asm src/boot/defines.asm
	@if not exist build mkdir build
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
	@if not exist $(subst /,\,$(@D)) mkdir $(subst /,\,$(@D))
	@echo CC   $<
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

build/%.o: src/%.asm
	@if not exist $(subst /,\,$(@D)) mkdir $(subst /,\,$(@D))
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

clean:
	@if exist build rmdir /s /q build
