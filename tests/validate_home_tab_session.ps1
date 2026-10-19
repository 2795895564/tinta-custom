# P1 regression: Home is a fixed tab at index 0 and session files survive.
#
# Pass 1: repo + two session tabs launch as [Home, a, b] with Home active;
#         closing the window must keep both tab= lines.
# Pass 2: Ctrl+Tab walks the tabs, Ctrl+W closes documents back to Home,
#         Ctrl+W on Home is a no-op, Ctrl+T returns to Home.
param(
    [string]$Binary = "$PSScriptRoot\..\build\Release\tinta.exe",
    [string]$Output = "$PSScriptRoot\..\out\home-tab-session"
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
Remove-Item -Recurse -Force $Output -ErrorAction SilentlyContinue
$appDir = Join-Path $Output 'app'
$repoDir = Join-Path $Output 'repo'
New-Item -ItemType Directory -Force $appDir, $repoDir | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $appDir 'tinta.exe') -Force
Set-Content -Encoding utf8 (Join-Path $repoDir 'a.md') "# A`n`nFirst session tab."
Set-Content -Encoding utf8 (Join-Path $repoDir 'b.md') "# B`n`nSecond session tab."

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class HomeTabSessionWin32 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@

function Write-Settings {
    $a = Join-Path $repoDir 'a.md'
    $b = Join-Path $repoDir 'b.md'
    $repo = "r1|0|Session probe|$repoDir|"
    $ini = @"
[Settings]
themeIndex=0
followSystemTheme=0
hasAskedFileAssociation=1
openInTabs=1
checkUpdates=0
language=en

[Repos]
startupMode=0
repoCurrent=r1
repo=$repo

[Session]
sessionActive=0
tab=$a
tab=$b
"@
    Set-Content -LiteralPath (Join-Path $appDir 'settings.ini') -Value $ini -Encoding utf8
}

function Wait-Window($process) {
    for ($i = 0; $i -lt 120 -and $process.MainWindowHandle -eq 0 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw 'Tinta did not create a window' }
    Start-Sleep -Milliseconds 1100
    [void][HomeTabSessionWin32]::SetForegroundWindow($process.MainWindowHandle)
    return $process.MainWindowHandle
}

function Key([IntPtr]$hwnd, [byte]$vk, [bool]$ctrl = $false) {
    if ($ctrl) {
        [HomeTabSessionWin32]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
    }
    [HomeTabSessionWin32]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [HomeTabSessionWin32]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    if ($ctrl) {
        Start-Sleep -Milliseconds 40
        [HomeTabSessionWin32]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 500
}

function Capture([IntPtr]$hwnd, [string]$name) {
    $rect = New-Object HomeTabSessionWin32+RECT
    [void][HomeTabSessionWin32]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap $w, $h
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    [void][HomeTabSessionWin32]::PrintWindow($hwnd, $hdc, 2)
    $graphics.ReleaseHdc($hdc)
    $graphics.Dispose()
    $path = Join-Path $Output $name
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    "capture $name : ${w}x${h}"
}

function Close($process) {
    if ($process -and -not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(8000)) { Stop-Process -Id $process.Id -Force }
    }
    Start-Sleep -Milliseconds 600
}

function Expect([string]$name, [bool]$ok, [string]$detail) {
    if ($ok) { "PASS  $name ($detail)"; return $true }
    "FAIL  $name ($detail)"; return $false
}

$failures = 0
$passed = 0

# Pass 1: Home + session files, no close of documents.
Write-Settings
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Capture $hwnd '01-home-with-session-tabs.png'
    $title = $process.MainWindowTitle
    if (Expect 'Home active on startup' ($title -notmatch 'a\.md|b\.md') $title) { $passed++ } else { $failures++ }
    Close $process
    $ini = Get-Content -Encoding utf8 (Join-Path $appDir 'settings.ini') -Raw
    $tabs = [regex]::Matches($ini, '(?m)^tab=')
    if (Expect 'session tabs survive close' ($tabs.Count -eq 2) "$($tabs.Count) tab lines") { $passed++ } else { $failures++ }
} catch {
    "FAIL  pass 1: $_"
    $failures++
} finally {
    Close $process
}

# Pass 2: tab traversal and close-to-Home.
Write-Settings
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Key $hwnd 0x09 $true             # Ctrl+Tab -> a.md
    if (Expect 'Ctrl+Tab reaches a.md' ($process.MainWindowTitle -match 'a\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x09 $true             # Ctrl+Tab -> b.md
    if (Expect 'Ctrl+Tab reaches b.md' ($process.MainWindowTitle -match 'b\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x57 $true             # Ctrl+W closes b.md
    if (Expect 'closing b.md returns to a.md' ($process.MainWindowTitle -match 'a\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x57 $true             # Ctrl+W closes a.md -> Home
    if (Expect 'closing the last file returns Home' ($process.MainWindowTitle -notmatch 'a\.md|b\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    $process.Refresh()
    if (Expect 'closing the last file keeps the process alive' (-not $process.HasExited) 'alive') { $passed++ } else { $failures++ }
    Key $hwnd 0x57 $true             # Ctrl+W on Home: no-op
    $process.Refresh()
    if (Expect 'Ctrl+W on Home is a no-op' ((-not $process.HasExited) -and ($process.MainWindowTitle -notmatch 'a\.md|b\.md')) $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x54 $true             # Ctrl+T -> Home
    if (Expect 'Ctrl+T stays on Home' ($process.MainWindowTitle -notmatch 'a\.md|b\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Capture $hwnd '02-home-after-close.png'
    Key $hwnd 0x0D                   # Enter opens the selected a.md
    if (Expect 'open from Home keeps a file tab' ($process.MainWindowTitle -match 'a\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x1B                   # Esc returns to the fixed Home tab
    if (Expect 'reader Esc returns to Home' ($process.MainWindowTitle -notmatch 'a\.md|b\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x57 $true             # Ctrl+W on Home is a no-op
    if (Expect 'Ctrl+W on Home after reader Esc is a no-op' ($process.MainWindowTitle -notmatch 'a\.md|b\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x0D                   # reopen a.md
    if (Expect 'reopen from Home works' ($process.MainWindowTitle -match 'a\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x57 $true             # Ctrl+W closes it, Home stays
    if (Expect 'closing that file returns Home' ($process.MainWindowTitle -notmatch 'a\.md|b\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    $process.Refresh()
    if (Expect 'Home survives open/close' (-not $process.HasExited) 'alive') { $passed++ } else { $failures++ }
    Capture $hwnd '03-home-after-open-close.png'
    # Home N now opens the file-operation modal; that is covered by
    # tests/validate_home_file_ops.ps1.
} catch {
    "FAIL  pass 2: $_"
    $failures++
} finally {
    Close $process
}

# Pass 3: a clean edit session must leave on a single Esc.
Write-Settings
$iniPath = Join-Path $appDir 'settings.ini'
(Get-Content -Encoding utf8 $iniPath -Raw) -replace 'openInTabs=1', 'openInTabs=0' |
    Set-Content -Encoding utf8 $iniPath
$editArg = Join-Path $repoDir 'a.md'
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') `
    -ArgumentList @('--edit', "`"$editArg`"") -PassThru
try {
    $hwnd = Wait-Window $process
    if (Expect '--edit starts in edit mode' ($process.MainWindowTitle -match 'a\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x1B                   # single Esc: clean edit -> reader
    Key $hwnd 0x57 $true             # Ctrl+W closes only in the reader
    if (Expect 'single Esc leaves edit mode' ($process.MainWindowTitle -notmatch 'a\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    $process.Refresh()
    if (Expect 'single Esc keeps the process alive' (-not $process.HasExited) 'alive') { $passed++ } else { $failures++ }
} catch {
    "FAIL  edit single-Esc pass: $_"
    $failures++
} finally {
    Close $process
}

"Home tab + session: $passed passed, $failures failed"
exit $failures
