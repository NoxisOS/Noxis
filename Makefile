# ─────────────────────────────────────────────────────────────
# Noxis OS Build System — x86_64 (port in progress)
#
# Core milestone: boot sector → long mode → 64-bit kernel that brings
# up GDT/IDT/PMM/VMM/heap.  Subsystems (proc, syscall, fs, userland)
# are being ported back in phase by phase.
# ─────────────────────────────────────────────────────────────

AS      = nasm

ifeq ($(OS),Windows_NT)
    TOOLS64 = D:/Program Files/x86_64-elf-tools-windows/bin
    CC      = "$(TOOLS64)/x86_64-elf-gcc"
    LD      = "$(TOOLS64)/x86_64-elf-ld"
    OBJCOPY = "$(TOOLS64)/x86_64-elf-objcopy"
    QEMU    = "D:\Program Files\qemu\qemu-system-x86_64"
    MKDIRP  = if not exist $(subst /,\,$1) mkdir $(subst /,\,$1)
    MAKE_BOOTIMG = powershell -NoProfile -ExecutionPolicy Bypass -Command "$$b=[IO.File]::ReadAllBytes('build/boot.bin'); $$k=[IO.File]::ReadAllBytes('build/kernel.bin'); $$img=New-Object byte[] (512*2048); [Array]::Copy($$b,0,$$img,0,512); [System.Buffer]::BlockCopy($$k,0,$$img,512,$$k.Length); [IO.File]::WriteAllBytes('build/noxis.img',$$img)"
    MAKE_NOXFS   = powershell -NoProfile -ExecutionPolicy Bypass -File tools/windows/build_disk.ps1
else
    SHELL   = /bin/sh
    CC      = x86_64-elf-gcc
    LD      = x86_64-elf-ld
    OBJCOPY = x86_64-elf-objcopy
    QEMU    = qemu-system-x86_64
    MKDIRP  = mkdir -p $1
    MAKE_BOOTIMG = dd if=/dev/zero of=$(DISK_IMG) bs=512 count=2048 status=none && \
                   dd if=$(BOOT_BIN) of=$(DISK_IMG) conv=notrunc status=none && \
                   dd if=$(KERNEL_BIN) of=$(DISK_IMG) bs=512 seek=1 conv=notrunc status=none
    MAKE_NOXFS   = tools/linux/build_disk.sh build/disk.img nsh.elf:build/nsh.elf ls.elf:build/ls.elf echo.elf:build/echo.elf cat.elf:build/cat.elf ps.elf:build/ps.elf
endif

# ── Kernel C flags (freestanding, no red zone, no SSE) ───────
CFLAGS  = -std=c11 -ffreestanding -nostdlib -nostdinc \
          -Wall -Wextra -Werror -Wno-unused-parameter \
          -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mgeneral-regs-only \
          -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables \
          -I src -O2
DEPFLAGS = -MMD -MP

# ── Core kernel objects (the ported subset) ──────────────────
KERNEL_C_OBJS = \
  build/kernel/core/early.o \
  build/drivers/serial.o    \
  build/drivers/pit.o       \
  build/drivers/vga.o       \
  build/drivers/kbd.o       \
  build/drivers/keymap.o    \
  build/drivers/ata.o       \
  build/drivers/block/block.o \
  build/kernel/hal/gdt.o    \
  build/kernel/hal/pic.o    \
  build/kernel/hal/fpu.o    \
  build/kernel/isr/isr.o    \
  build/mm/phys/pmm.o       \
  build/mm/virt/vmm.o       \
  build/mm/virt/heap.o      \
  build/mm/slab.o           \
  build/mm/arena.o          \
  build/proc/process.o      \
  build/proc/scheduler.o    \
  build/proc/syscalls.o     \
  build/proc/fd.o           \
  build/proc/pipe.o         \
  build/proc/signal.o       \
  build/proc/elf.o          \
  build/fs/noxfs/buffer.o   \
  build/fs/noxfs/noxfs.o    \
  build/fs/vfs/ramfs.o      \
  build/fs/vfs/vfs.o        \
  build/kernel/syscall/syscall.o

KERNEL_ASM_OBJS = \
  build/boot/kernel_entry.o      \
  build/kernel/hal/gdt_load.o    \
  build/kernel/hal/idt_load.o    \
  build/kernel/isr/isr_stubs.o   \
  build/proc/kthread_switch.o    \
  build/proc/user_enter.o        \
  build/kernel/syscall/syscall_entry.o

KERNEL_OBJS = $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

KERNEL_ELF = build/kernel.elf
KERNEL_BIN = build/kernel.bin
BOOT_BIN   = build/boot.bin
DISK_IMG   = build/noxis.img

NOXFS_IMG  = build/disk.img

.PHONY: all clean run
all: $(DISK_IMG) $(NOXFS_IMG)

# ── Compile rules ─────────────────────────────────────────────
build/%.o: src/%.c
	@$(call MKDIRP,$(@D))
	@echo CC   $<
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

build/%.o: src/%.asm
	@$(call MKDIRP,$(@D))
	@echo AS   $<
	$(AS) -f elf64 $< -o $@

# ── Boot sector (flat binary) ─────────────────────────────────
$(BOOT_BIN): src/boot/boot.asm
	@$(call MKDIRP,build)
	@echo AS   $@
	$(AS) -f bin $< -o $@

# ── Userland ELF64 (C program + crt0) ───────────────────────────
UCFLAGS = -std=c11 -ffreestanding -nostdlib -nostdinc -m64 \
          -fno-pic -fno-stack-protector -Wall -Wextra -O2 -I src

build/u_crt0.o: src/lib/noxlib/crt0.asm
	@$(call MKDIRP,build)
	$(AS) -f elf64 $< -o $@

# Userland programs: one per src/bin/<name>/<name>.c. Rules are generated per
# program so each links crt0 + its object against the shared user.ld.
PROGS     = nsh ls echo cat ps mkdir rm mv
USER_ELFS = $(addprefix build/,$(addsuffix .elf,$(PROGS)))

define PROG_RULE
build/u_$(1).o: src/bin/$(1)/$(1).c src/lib/noxlib/noxlib.h
	@$$(call MKDIRP,build)
	$$(CC) $$(UCFLAGS) -c $$< -o $$@
build/$(1).elf: build/u_crt0.o build/u_$(1).o src/lib/noxlib/user.ld
	@echo LD   $$@
	$$(LD) -T src/lib/noxlib/user.ld -nostdlib -o $$@ build/u_crt0.o build/u_$(1).o
endef
$(foreach p,$(PROGS),$(eval $(call PROG_RULE,$(p))))

# ── Kernel link + flatten ─────────────────────────────────────
$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld
	@echo LD   $@
	$(LD) -T linker.ld -nostdlib -o $@ $(KERNEL_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo BIN  $@
	$(OBJCOPY) -O binary $< $@

# ── Disk image: sector 0 = boot, sector 1+ = kernel ──────────
$(DISK_IMG): $(BOOT_BIN) $(KERNEL_BIN)
	@echo BUILD $@
	$(MAKE_BOOTIMG)

# NoxFS disk image (hdb = primary slave) built from the userland ELFs.
$(NOXFS_IMG): $(USER_ELFS) tools/windows/build_disk.ps1 tools/linux/build_disk.sh
	@echo BUILD $@
	$(MAKE_NOXFS)

run: $(DISK_IMG) $(NOXFS_IMG)
	@echo RUN  QEMU x86_64
	$(QEMU) -drive file=build/noxis.img,format=raw,index=0 \
	        -drive file=build/disk.img,format=raw,index=1 -no-reboot -serial stdio

run-headless: $(DISK_IMG) $(NOXFS_IMG)
	$(QEMU) -drive file=build/noxis.img,format=raw,index=0 \
	        -drive file=build/disk.img,format=raw,index=1 -no-reboot -display none -serial stdio

-include $(wildcard build/**/*.d build/*.d)

clean:
	@if exist build rmdir /s /q build
