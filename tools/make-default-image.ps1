# Draws resources\default-image.png: the picture the camera shows when no image was published and
# no screen zone is broadcast. It is embedded in CameraEmulatorSource.dll (see
# src\source\CameraEmulatorSource.rc). Re-run after changing this script, then rebuild.
#
#   powershell -ExecutionPolicy Bypass -File tools\make-default-image.ps1

Add-Type -AssemblyName System.Drawing

$size = 800 # square: fits both the "android" (portrait) and "fit" (landscape) layouts
$output = Join-Path $PSScriptRoot "..\resources\default-image.png"
New-Item -ItemType Directory -Force -Path (Split-Path $output) | Out-Null

$bitmap = New-Object System.Drawing.Bitmap $size, $size
$g = [System.Drawing.Graphics]::FromImage($bitmap)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

function New-RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($x, $y, 2 * $r, 2 * $r, 180, 90)
    $path.AddArc($x + $w - 2 * $r, $y, 2 * $r, 2 * $r, 270, 90)
    $path.AddArc($x + $w - 2 * $r, $y + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $path.AddArc($x, $y + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $path.CloseFigure()
    return $path
}

# Background: dark blue vertical gradient.
$background = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Rectangle 0, 0, $size, $size),
    ([System.Drawing.Color]::FromArgb(255, 32, 46, 66)), ([System.Drawing.Color]::FromArgb(255, 14, 20, 30)), 90
$g.FillRectangle($background, 0, 0, $size, $size)

# Camera, same design as the tray icon: dark outline, light body, blue lens.
$dark = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 45, 45, 45))
$light = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 240, 240, 240))
$blue = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0, 120, 215))
$cx = 400; $top = 120
$g.FillPath($light, (New-RoundedRect ($cx - 70) ($top - 22) 120 40 8))       # viewfinder bump
$g.FillPath($light, (New-RoundedRect ($cx - 160) $top 320 210 28))            # body
$g.FillEllipse($dark, $cx - 78, $top + 27, 156, 156)                          # lens ring
$g.FillEllipse($blue, $cx - 52, $top + 53, 104, 104)                          # lens
$g.FillEllipse($light, $cx - 30, $top + 70, 26, 26)                           # glint

# Text.
$center = New-Object System.Drawing.StringFormat
$center.Alignment = [System.Drawing.StringAlignment]::Center
$white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
$grey = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 170, 185, 205))
$accent = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 96, 170, 255))
$title = New-Object System.Drawing.Font "Segoe UI", 54, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
$subtitle = New-Object System.Drawing.Font "Segoe UI", 34, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$hint = New-Object System.Drawing.Font "Segoe UI", 25, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$code = New-Object System.Drawing.Font "Consolas", 24, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)

$g.DrawString("CameraEmulator", $title, $white, (New-Object System.Drawing.RectangleF 0, 380, $size, 80), $center)
$g.DrawString("No image selected", $subtitle, $accent, (New-Object System.Drawing.RectangleF 0, 460, $size, 50), $center)

$line = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 60, 78, 102)), 2
$g.DrawLine($line, 170, 540, 630, 540)

$g.DrawString("Tray menu: Choose image... or Start broadcast", $hint, $grey, (New-Object System.Drawing.RectangleF 0, 570, $size, 40), $center)
$g.DrawString("Command line:", $hint, $grey, (New-Object System.Drawing.RectangleF 0, 625, $size, 40), $center)
$g.DrawString("CameraEmulator.exe image.png", $code, $white, (New-Object System.Drawing.RectangleF 0, 662, $size, 40), $center)
$g.DrawString("CameraEmulator.exe --screen 300x300", $code, $white, (New-Object System.Drawing.RectangleF 0, 700, $size, 40), $center)

$bitmap.Save($output, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bitmap.Dispose()
Write-Output "Wrote $((Resolve-Path $output).Path)"
