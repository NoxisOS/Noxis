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

$mbr    = [IO.File]::ReadAllBytes($Mbr)
$loader = [IO.File]::ReadAllBytes($Loader)
$kernel = [IO.File]::ReadAllBytes($Kernel)

# Create blank 1.44 MB image (2880 sectors x 512)
$fs = New-Object IO.FileStream($Out, [IO.FileMode]::Create)
$fs.SetLength(1474560)

# MBR at sector 0 (offset 0)
$fs.Position = 0
$fs.Write($mbr, 0, $mbr.Length)

# Stage-2 loader at sector 1 (offset 512)
$fs.Position = 512
$fs.Write($loader, 0, $loader.Length)

# Kernel at sector 5 (offset 2560)
$fs.Position = 2560
$fs.Write($kernel, 0, $kernel.Length)

$fs.Dispose()
Write-Host "  -> $Out  (floppy 1.44 MB)"
