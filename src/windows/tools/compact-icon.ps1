# Compact the existing multi-resolution icon *for the native WinAPI EXE only*.
# Preserve the original PNG bytes in the retained frames. No image resampling.
# Retain 16, 32, 48 and 128px widths for taskbar, titlebar and Explorer.
# The source .ico and the default Windows DLL/resource builds are untouched.
param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = "Stop"
$source = [System.IO.File]::ReadAllBytes($InputPath)
if ($source.Length -lt 6 -or
    [BitConverter]::ToUInt16($source, 0) -ne 0 -or
    [BitConverter]::ToUInt16($source, 2) -ne 1) {
    throw "Source is not a valid ICO file"
}
$count = [int][BitConverter]::ToUInt16($source, 4)
if ($source.Length -lt (6 + 16 * $count)) {
    throw "ICO directory is truncated"
}

$retain = @(16, 32, 48, 128)
$frames = @()
for ($i = 0; $i -lt $count; $i++) {
    $start = 6 + $i * 16
    $width = [int]$source[$start]
    if ($width -eq 0) { $width = 256 }
    $length = [uint64][BitConverter]::ToUInt32($source, $start + 8)
    $offset = [uint64][BitConverter]::ToUInt32($source, $start + 12)
    if ($offset -gt $source.Length -or $length -gt ($source.Length - $offset)) {
        throw "ICO frame has invalid byte range"
    }
    if ($retain -contains $width) {
        $frames += [pscustomobject]@{
            EntryOffset = $start
            Width = $width
            ByteOffset = [int]$offset
            ByteLength = [int]$length
        }
    }
}
if ($frames.Count -ne 4) {
    throw "Expected 16, 32, 48, and 128 pixel icon frames (found $($frames.Count))"
}

[int]$outputLength = 6 + 16 * $frames.Count
foreach ($frame in $frames) { $outputLength += $frame.ByteLength }
$output = New-Object byte[] $outputLength
[Array]::Copy($source, 0, $output, 0, 6)
[Array]::Copy([BitConverter]::GetBytes([uint16]$frames.Count), 0, $output, 4, 2)

$cursor = 6 + 16 * $frames.Count
for ($i = 0; $i -lt $frames.Count; $i++) {
    $frame = $frames[$i]
    $entryOffset = 6 + 16 * $i
    [Array]::Copy($source, $frame.EntryOffset, $output, $entryOffset, 16)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$cursor), 0,
                  $output, $entryOffset + 12, 4)
    [Array]::Copy($source, $frame.ByteOffset, $output, $cursor,
                  $frame.ByteLength)
    $cursor += $frame.ByteLength
}

[System.IO.File]::WriteAllBytes($OutputPath, $output)
Write-Host ("[icon] {0} -> {1} bytes ({2} frames, original artwork)" -f
            $source.Length, $output.Length, $frames.Count)
