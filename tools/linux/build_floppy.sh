#!/usr/bin/env bash
# tools/linux/build_floppy.sh
# Creates a 1.44 MB floppy image from MBR + loader + kernel binaries.
#
# Usage:
#   tools/linux/build_floppy.sh <out> <mbr> <loader> <kernel>

set -e

OUT=$1
MBR=$2
LOADER=$3
KERNEL=$4

if [ -z "$OUT" ] || [ -z "$MBR" ] || [ -z "$LOADER" ] || [ -z "$KERNEL" ]; then
    echo "usage: build_floppy.sh <out> <mbr> <loader> <kernel>" >&2
    exit 1
fi

# Create blank 1.44 MB image
dd if=/dev/zero of="$OUT" bs=512 count=2880 status=none

# MBR at offset 0 (sector 0)
dd if="$MBR" of="$OUT" bs=512 count=1 conv=notrunc status=none

# Stage-2 loader at offset 512 (sector 1)
dd if="$LOADER" of="$OUT" bs=512 seek=1 conv=notrunc status=none

# Kernel at offset 2560 (sector 5)
dd if="$KERNEL" of="$OUT" bs=512 seek=5 conv=notrunc status=none

echo "  -> $OUT  (floppy 1.44 MB)"
