# GDB initialization for Noxis kernel debugging
# Usage: i686-elf-gdb -x .gdbinit

# Connect to QEMU GDB server
target remote localhost:1234

# Load kernel symbols
symbol-file build/kernel.elf

# Set a hardware breakpoint at kernel entry
hbreak _start

# Continue execution (will stop at _start)
continue
