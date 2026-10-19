# Home B Miller-column layout validation.
#
# Launches a portable copy of Tinta against the home-columns fixture, drives
# the column browser with keyboard messages, and captures PrintWindow images
# at normal and narrow widths in a light and a dark theme. Screenshots stay in
# the ignored out/ tree.
param(
    [string]$Binary = "$PSScriptRoot\..\build\Release\tinta.exe",
    [string]$Output = "$PSScriptRoot\..\out\home-columns"
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
Remove-Item -Recurse -Force $Output -ErrorAction SilentlyContinue
$appDir = Join-Path $Output 'app'
$repoDir = Join-Path $Output 'repo'
New-Item -ItemType Directory -Force $appDir, $repoDir | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $appDir 'tinta.exe') -Force
Copy-Item -Path (Join-Path $PSScriptRoot 'fixtures\home-columns\*') -Destination $repoDir -Recurse -Force
# An image row exercises the IMG badge; generated notes give the column more
# rows than fit so the thin scroll rail is visible.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fixtures\preview-follow-img-01.png') `
    -Destination (Join-Path $repoDir 'preview.png') -Force
$readme = Join-Path $repoDir 'README.md'
for ($i = 1; $i -le 24; $i++) {
    Copy-Item -LiteralPath $readme -Destination (Join-Path $repoDir ('note-{0:D2}.md' -f $i)) -Force
}

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class HomeColumnsWin32 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int w, int h, bool repaint);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr hWnd, ref POINT point);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

function Write-Settings([int]$theme, [string]$lang = 'en') {
    $repo = "r1|0|Home columns|$repoDir|"
    $ini = @"
[Settings]
themeIndex=$theme
followSystemTheme=0
hasAskedFileAssociation=1
openInTabs=0
checkUpdates=0
language=$lang

[Repos]
startupMode=0
repoCurrent=r1
repo=$repo
"@
    Set-Content -LiteralPath (Join-Path $appDir 'settings.ini') -Value $ini -Encoding utf8
}

function Wait-Window($process) {
    for ($i = 0; $i -lt 120 -and $process.MainWindowHandle -eq 0 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw 'Tinta did not create a window' }
    Start-Sleep -Milliseconds 900
    return $process.MainWindowHandle
}

function Key([IntPtr]$hwnd, [int]$vk) {
    [void][HomeColumnsWin32]::PostMessage($hwnd, 0x0100, [IntPtr]$vk, [IntPtr]0)
    [void][HomeColumnsWin32]::PostMessage($hwnd, 0x0101, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 450
}

function Capture([IntPtr]$hwnd, [string]$name) {
    $rect = New-Object HomeColumnsWin32+RECT
    [void][HomeColumnsWin32]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    if ($w -lt 200 -or $h -lt 200) { throw "bad window size for $name" }
    $bitmap = New-Object System.Drawing.Bitmap $w, $h
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    [void][HomeColumnsWin32]::PrintWindow($hwnd, $hdc, 2)
    $graphics.ReleaseHdc($hdc)
    $graphics.Dispose()
    $path = Join-Path $Output $name
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    if (!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -lt 20000) {
        throw "empty capture: $name"
    }
    "$name : ${w}x${h}, $((Get-Item -LiteralPath $path).Length) bytes"
}

function Size-Window([IntPtr]$hwnd, [int]$w, [int]$h) {
    [void][HomeColumnsWin32]::MoveWindow($hwnd, 70, 70, $w, $h, $true)
    Start-Sleep -Milliseconds 900
}

function Hover([IntPtr]$hwnd, [int]$x, [int]$y) {
    $lp = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [void][HomeColumnsWin32]::PostMessage($hwnd, 0x0200, [IntPtr]0, $lp)
    Start-Sleep -Milliseconds 500
}

function Close($process) {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        [void]$process.WaitForExit(5000)
    }
}

$failures = 0
$results = @()

# Light theme, normal width.
Write-Settings 0
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Size-Window $hwnd 2400 1300
    $results += Capture $hwnd '01-single-2400.png'
    Key $hwnd 0x0D            # open projects -> two columns
    $results += Capture $hwnd '02-two-2400.png'
    Key $hwnd 0x0D            # open design -> three columns
    $results += Capture $hwnd '03-three-2400.png'
    Key $hwnd 0x25; Key $hwnd 0x25   # back to the root column
    Key $hwnd 0x28            # select the first file row
    $results += Capture $hwnd '04-single-file-2400.png'
    Size-Window $hwnd 1440 900
    $results += Capture $hwnd '05-single-1440.png'
    Hover $hwnd 300 330
    $results += Capture $hwnd '06-hover-1440.png'
    Size-Window $hwnd 760 820
    $results += Capture $hwnd '07-single-760.png'
} catch {
    $results += "FAIL light pass: $_"
    $failures++
} finally {
    Close $process
}

# Dark theme (Midnight): same structure, different surfaces.
Write-Settings 5
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Size-Window $hwnd 2400 1300
    $results += Capture $hwnd '08-dark-single-2400.png'
    Key $hwnd 0x0D
    Key $hwnd 0x0D
    $results += Capture $hwnd '09-dark-three-2400.png'
} catch {
    $results += "FAIL dark pass: $_"
    $failures++
} finally {
    Close $process
}

# Simplified Chinese pass: item counts and the selected-folder footer label
# are translated, while file and folder names stay untouched.
Write-Settings 0 'zh'
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Size-Window $hwnd 1440 900
    $results += Capture $hwnd '10-zh-single-1440.png'
    Key $hwnd 0x0D
    Key $hwnd 0x0D
    $results += Capture $hwnd '11-zh-three-1440.png'
} catch {
    $results += "FAIL zh pass: $_"
    $failures++
} finally {
    Close $process
}

$results
"Home columns: $failures failures"
exit $failures
