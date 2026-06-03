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
    MAKE_NOXFS   = tools/linux/build_disk.sh build/disk.img hello.elf:build/hello.elf child.elf:build/child.elf motd.txt:src/userland64/motd.txt
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
  build/proc/elf.o          \
  build/fs/noxfs/buffer.o   \
  build/fs/noxfs/noxfs.o    \
  build/fs/vfs/ramfs.o      \
  build/fs/vfs/vfs.o        \
  build/kernel/syscall/syscall64.o

KERNEL_ASM_OBJS = \
  build/boot/kernel_entry.o      \
  build/kernel/hal/gdt_load.o    \
  build/kernel/hal/idt_load.o    \
  build/kernel/isr/isr_stubs.o   \
  build/proc/kthread_switch.o    \
  build/proc/user_enter.o        \
  build/kernel/syscall/syscall_entry.o \
  build/userland64/hello_blob.o

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

# ── Userland ELF64 (C program + crt0, embedded into the kernel) ──
UCFLAGS = -std=c11 -ffreestanding -nostdlib -nostdinc -m64 \
          -fno-pic -fno-stack-protector -Wall -Wextra -O2

build/u_crt0.o: src/noxlib64/crt0.asm
	@$(call MKDIRP,build)
	$(AS) -f elf64 $< -o $@

build/u_hello.o: src/userland64/hello.c
	@$(call MKDIRP,build)
	$(CC) $(UCFLAGS) -c $< -o $@

build/hello.elf: build/u_crt0.o build/u_hello.o src/userland64/user.ld
	@echo LD   $@
	$(LD) -T src/userland64/user.ld -nostdlib -o $@ build/u_crt0.o build/u_hello.o

build/u_child.o: src/userland64/child.c
	@$(call MKDIRP,build)
	$(CC) $(UCFLAGS) -c $< -o $@

build/child.elf: build/u_crt0.o build/u_child.o src/userland64/user.ld
	@echo LD   $@
	$(LD) -T src/userland64/user.ld -nostdlib -o $@ build/u_crt0.o build/u_child.o

# The blob incbin's build/hello.elf, so it must exist first.
build/userland64/hello_blob.o: src/userland64/hello_blob.asm build/hello.elf
	@$(call MKDIRP,build/userland64)
	$(AS) -f elf64 $< -o $@

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
$(NOXFS_IMG): build/hello.elf build/child.elf src/userland64/motd.txt tools/windows/build_disk.ps1 tools/linux/build_disk.sh
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
