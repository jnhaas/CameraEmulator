# Draws resources\CameraEmulator.ico: the application icon of CameraEmulatorTray.exe and
# CameraEmulator.exe (Start menu, Explorer, taskbar). Same design as the tray icon drawn at runtime
# (src\tray\TrayApp.cpp, CreateCameraIcon) and the default image. Every size is drawn separately so
# small sizes stay sharp. 256 px is stored as PNG, smaller sizes as 32-bit bitmaps with alpha.
# Re-run after changing this script, then rebuild.
#
#   powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1

Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$output = Join-Path $PSScriptRoot "..\resources\CameraEmulator.ico"

function New-RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = [Math]::Max(2 * $r, 0.01)
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-IconPng([int]$size) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    $dark = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 45, 45, 45))
    $light = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 245, 245, 245))
    $blue = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0, 120, 215))
    $s = [float]$size

    # Proportions shared with the tray icon: dark outline and viewfinder, light body, lens.
    $g.FillPath($dark, (New-RoundedRect (0.30 * $s) (0.15 * $s) (0.30 * $s) (0.17 * $s) (0.03 * $s)))
    $g.FillPath($dark, (New-RoundedRect (0.05 * $s) (0.26 * $s) (0.90 * $s) (0.62 * $s) (0.12 * $s)))
    $g.FillPath($light, (New-RoundedRect (0.12 * $s) (0.33 * $s) (0.76 * $s) (0.48 * $s) (0.07 * $s)))
    $g.FillEllipse($dark, 0.29 * $s, 0.36 * $s, 0.42 * $s, 0.42 * $s)
    $g.FillEllipse($blue, 0.38 * $s, 0.45 * $s, 0.24 * $s, 0.24 * $s)
    if ($size -ge 32) {
        # Glint on the lens, only where it is visible.
        $g.FillEllipse($light, 0.42 * $s, 0.49 * $s, 0.06 * $s, 0.06 * $s)
    }

    $g.Dispose()
    if ($size -ge 256) { $data = Get-PngBytes $bitmap } else { $data = Get-DibBytes $bitmap }
    $bitmap.Dispose()
    return , $data # comma: keep the byte[] as one object
}

# 256 px: PNG, as Windows expects.
function Get-PngBytes([System.Drawing.Bitmap]$bitmap) {
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    return , $stream.ToArray()
}

# Smaller sizes: classic icon bitmap, readable by every tool. A BITMAPINFOHEADER with a double
# height, bottom-up 32-bit BGRA pixels (alpha used), then an all-zero 1-bit AND mask.
function Get-DibBytes([System.Drawing.Bitmap]$bitmap) {
    $size = $bitmap.Width
    $rect = New-Object System.Drawing.Rectangle 0, 0, $size, $size
    $locked = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $pixels = New-Object byte[] ($size * $size * 4)
    for ($y = 0; $y -lt $size; $y++) {
        # Copy rows bottom-up.
        $source = [IntPtr]($locked.Scan0.ToInt64() + ($size - 1 - $y) * $locked.Stride)
        [System.Runtime.InteropServices.Marshal]::Copy($source, $pixels, $y * $size * 4, $size * 4)
    }
    $bitmap.UnlockBits($locked)
    $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)

    $stream = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter $stream
    $w.Write([UInt32]40); $w.Write([Int32]$size); $w.Write([Int32]($size * 2))
    $w.Write([UInt16]1); $w.Write([UInt16]32); $w.Write([UInt32]0)
    $w.Write([UInt32]($pixels.Length + $maskStride * $size))
    $w.Write([Int32]0); $w.Write([Int32]0); $w.Write([UInt32]0); $w.Write([UInt32]0)
    $w.Write($pixels)
    $w.Write((New-Object byte[] ($maskStride * $size)))
    $w.Flush()
    return , $stream.ToArray()
}

$images = @()
foreach ($size in $sizes) { $images += , [byte[]](New-IconPng $size) }

# ICO file: ICONDIR, one ICONDIRENTRY per image, then the PNG data.
New-Item -ItemType Directory -Force -Path (Split-Path $output) | Out-Null
$file = [System.IO.File]::Create($output)
$writer = New-Object System.IO.BinaryWriter $file
$writer.Write([UInt16]0)              # reserved
$writer.Write([UInt16]1)              # type: icon
$writer.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dimension = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] } # 0 means 256
    $writer.Write([Byte]$dimension)   # width
    $writer.Write([Byte]$dimension)   # height
    $writer.Write([Byte]0)            # palette colors
    $writer.Write([Byte]0)            # reserved
    $writer.Write([UInt16]1)          # color planes
    $writer.Write([UInt16]32)         # bits per pixel
    $writer.Write([UInt32]$images[$i].Length)
    $writer.Write([UInt32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write([byte[]]$image) }
$writer.Close()
Write-Output "Wrote $((Resolve-Path $output).Path) ($($sizes -join ', ') px)"
