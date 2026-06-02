# tools/windows/build_floppy.ps1
# Creates a 1.44 MB floppy image from MBR + loader + kernel binaries.
#
# Usage:
#   powershell -File tools/windows/build_floppy.ps1 `
#       -Out build/noxis.img -Mbr build/mbr.bin `
#       -Loader build/loader.bin -Kernel build/kernel.bin

param(
    [string]$Out    = 'build/noxis.img',
    [string]$Mbr    = 'build/mbr.bin',
    [string]$Loader = 'build/loader.bin',
    [string]$Kernel = 'build/kernel.bin'
)

$ErrorActionPreference = 'Stop'
$FLOPPY_SIZE = 1474560  # 1.44 MB = 2880 sectors x 512

$img = New-Object byte[] $FLOPPY_SIZE

$mbr    = [IO.File]::ReadAllBytes($Mbr)
$loader = [IO.File]::ReadAllBytes($Loader)
$kernel = [IO.File]::ReadAllBytes($Kernel)

# MBR at sector 0 (offset 0)
[Array]::Copy($mbr,    0, $img,    0, [Math]::Min($mbr.Length,    512))
# Stage-2 loader at sector 1 (offset 512)
[Array]::Copy($loader, 0, $img,  512, $loader.Length)
# Kernel at sector 5 (offset 2560)
[Array]::Copy($kernel, 0, $img, 2560, $kernel.Length)

[IO.File]::WriteAllBytes($Out, $img)
Write-Host "  -> $Out  (floppy 1.44 MB)"
