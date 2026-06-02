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

# NOTE: variable names differ from params ($Mbr/$Loader/$Kernel) to avoid
# PowerShell's case-insensitive variable aliasing converting byte[] to string.
[byte[]]$mbrBytes    = [System.IO.File]::ReadAllBytes($Mbr)
[byte[]]$loaderBytes = [System.IO.File]::ReadAllBytes($Loader)
[byte[]]$kernelBytes = [System.IO.File]::ReadAllBytes($Kernel)

# Build 1.44 MB floppy (2880 sectors x 512) in memory
[byte[]]$img = New-Object byte[] 1474560

# MBR at sector 0 (offset 0)
[System.Buffer]::BlockCopy($mbrBytes,    0, $img,    0, $mbrBytes.Length)
# Stage-2 loader at sector 1 (offset 512)
[System.Buffer]::BlockCopy($loaderBytes, 0, $img,  512, $loaderBytes.Length)
# Kernel at sector 5 (offset 2560)
[System.Buffer]::BlockCopy($kernelBytes, 0, $img, 2560, $kernelBytes.Length)

[System.IO.File]::WriteAllBytes((Join-Path (Get-Location) $Out), $img)
Write-Host "  -> $Out  (floppy 1.44 MB)"
