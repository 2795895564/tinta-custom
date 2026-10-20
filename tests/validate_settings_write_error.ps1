# Settings write failures must be visible (invariant 7).
#
# Makes settings.ini read-only, then navigates in Home so the app saves
# "last directory". The file must stay untouched and the app must stay alive;
# the screenshot shows the error chip.
param(
    [string]$Binary = "$PSScriptRoot\..\build\Release\tinta.exe",
    [string]$Output = "$PSScriptRoot\..\out\settings-readonly"
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
Remove-Item -Recurse -Force $Output -ErrorAction SilentlyContinue
$appDir = Join-Path $Output 'app'
$repoDir = Join-Path $Output 'repo'
$subDir = Join-Path $repoDir 'notes'
New-Item -ItemType Directory -Force $appDir, $repoDir, $subDir | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $appDir 'tinta.exe') -Force
Set-Content -Encoding utf8 (Join-Path $subDir 'a.md') "# A`n`nRead-only settings probe."

$repo = "r1|0|Write probe|$repoDir|"
$ini = @"
[Settings]
themeIndex=0
followSystemTheme=0
hasAskedFileAssociation=1
openInTabs=0
checkUpdates=0
language=en

[Repos]
startupMode=0
repoCurrent=r1
repo=$repo
"@
$iniPath = Join-Path $appDir 'settings.ini'
Set-Content -LiteralPath $iniPath -Value $ini -Encoding utf8
$before = (Get-Content -LiteralPath $iniPath -Raw)
Set-ItemProperty -LiteralPath $iniPath -Name IsReadOnly -Value $true

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class ReadOnlySettingsWin32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, ref RECT rect);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
Add-Type -AssemblyName System.Drawing

function Key([IntPtr]$hwnd, [int]$vk) {
    [void][ReadOnlySettingsWin32]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 200
    [ReadOnlySettingsWin32]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)
    [ReadOnlySettingsWin32]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 800
}

function Capture([IntPtr]$hwnd, [string]$name) {
    $rect = New-Object ReadOnlySettingsWin32+RECT
    [void][ReadOnlySettingsWin32]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap $w, $h
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    [void][ReadOnlySettingsWin32]::PrintWindow($hwnd, $hdc, 2)
    $graphics.ReleaseHdc($hdc)
    $graphics.Dispose()
    $path = Join-Path $Output $name
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    return $path
}

$failures = 0
$passed = 0
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    for ($i = 0; $i -lt 120 -and $process.MainWindowHandle -eq 0 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw 'Tinta did not create a window' }
    Start-Sleep -Milliseconds 900
    $hwnd = $process.MainWindowHandle

    Key $hwnd 0x0D                       # enter the first row (notes/)
    Key $hwnd 0x0D                       # open a.md in the reader
    Key $hwnd 0x54                       # T: theme chooser
    $shot0 = Capture $hwnd '00-theme-chooser.png'
    "capture: $shot0"
    Key $hwnd 0x0D                       # Enter: apply the theme (persists)
    $process.Refresh()
    if ($process.HasExited) {
        "FAIL  read-only settings : app exited while navigating"
        $failures++
    } else {
        "PASS  read-only settings : app survived the failed save"
        $passed++
    }
    $shot = Capture $hwnd '01-settings-readonly.png'
    "capture: $shot"

    $after = (Get-Content -LiteralPath $iniPath -Raw)
    if ($after -ne $before) {
        "FAIL  read-only settings : settings.ini changed despite read-only"
        $failures++
    } else {
        "PASS  read-only settings : settings.ini untouched"
        $passed++
    }
} catch {
    "FAIL  read-only pass: $_"
    $failures++
} finally {
    if ($process -and -not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(4000)) { Stop-Process -Id $process.Id -Force }
    }
    # Leave the temp copy writable so cleanup can remove it.
    if (Test-Path -LiteralPath $iniPath) {
        Set-ItemProperty -LiteralPath $iniPath -Name IsReadOnly -Value $false
    }
}

"Settings write error: $passed passed, $failures failed"
exit $failures
