# P1 edge cases: picker startup + session, file argument + repo, openInTabs=false.
#
# These are deliberately diagnostic: the current contract is "workspace startup
# owns the first window; a file argument keeps the document-window behavior".
param(
    [string]$Binary = "$PSScriptRoot\..\build\Release\tinta.exe",
    [string]$Output = "$PSScriptRoot\..\out\home-startup-edges"
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
Remove-Item -Recurse -Force $Output -ErrorAction SilentlyContinue
$appDir = Join-Path $Output 'app'
$repoDir = Join-Path $Output 'repo'
New-Item -ItemType Directory -Force $appDir, $repoDir | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $appDir 'tinta.exe') -Force
Set-Content -Encoding utf8 (Join-Path $repoDir 'a.md') "# A`n`nPicker/session edge."
Set-Content -Encoding utf8 (Join-Path $repoDir 'b.md') "# B`n`nSecond session tab."

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class HomeStartupEdgesWin32 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@

function Write-Settings([int]$StartupMode, [int]$OpenInTabs) {
    $a = Join-Path $repoDir 'a.md'
    $b = Join-Path $repoDir 'b.md'
    $ini = @"
[Settings]
themeIndex=0
followSystemTheme=0
hasAskedFileAssociation=1
openInTabs=$OpenInTabs
checkUpdates=0
language=en

[Repos]
startupMode=$StartupMode
repoCurrent=r1
repo=r1|0|Edge probe|$repoDir|

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
    Start-Sleep -Milliseconds 1000
    [void][HomeStartupEdgesWin32]::SetForegroundWindow($process.MainWindowHandle)
    return $process.MainWindowHandle
}

function Key([IntPtr]$hwnd, [byte]$vk, [bool]$ctrl = $false) {
    if ($ctrl) {
        [HomeStartupEdgesWin32]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
    }
    [HomeStartupEdgesWin32]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [HomeStartupEdgesWin32]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    if ($ctrl) {
        Start-Sleep -Milliseconds 40
        [HomeStartupEdgesWin32]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 600
}

function Capture([IntPtr]$hwnd, [string]$name) {
    $rect = New-Object HomeStartupEdgesWin32+RECT
    [void][HomeStartupEdgesWin32]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap $w, $h
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    [void][HomeStartupEdgesWin32]::PrintWindow($hwnd, $hdc, 2)
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

# A. Picker startup still keeps Home at index 0 and the session to its right.
Write-Settings 1 1
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Capture $hwnd '01-picker-session.png'
    if (Expect 'picker startup activates Home' ($process.MainWindowTitle -notmatch 'a\.md|b\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x09 $true
    if (Expect 'picker: Ctrl+Tab reaches a.md' ($process.MainWindowTitle -match 'a\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x09 $true
    if (Expect 'picker: Ctrl+Tab reaches b.md' ($process.MainWindowTitle -match 'b\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Close $process
    $ini = Get-Content -Encoding utf8 (Join-Path $appDir 'settings.ini') -Raw
    $tabs = [regex]::Matches($ini, '(?m)^tab=')
    if (Expect 'picker: session tabs survive close' ($tabs.Count -eq 2) "$($tabs.Count) tab lines") { $passed++ } else { $failures++ }
} catch {
    "FAIL  picker pass: $_"
    $failures++
} finally {
    Close $process
}

# B. A file argument with openInTabs=false opens its own document window;
#    current contract does not force Home (openInTabs=true joins an existing
#    window by design, so this pass isolates the document-window path).
Write-Settings 0 0
$fileA = Join-Path $repoDir 'a.md'
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') `
    -ArgumentList @("`"$fileA`"") -PassThru
try {
    $hwnd = Wait-Window $process
    Capture $hwnd '02-file-argument.png'
    if (Expect 'file argument opens a.md' ($process.MainWindowTitle -match 'a\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x09 $true
    if (Expect 'file argument does not inject the session' ($process.MainWindowTitle -match 'a\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    $process.Refresh()
    if (Expect 'file argument window stays alive' (-not $process.HasExited) 'alive') { $passed++ } else { $failures++ }
} catch {
    "FAIL  file-argument pass: $_"
    $failures++
} finally {
    Close $process
}

# C. openInTabs=false keeps the session parked; Home still owns the window.
Write-Settings 0 0
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Capture $hwnd '03-open-in-tabs-off.png'
    if (Expect 'openInTabs=false activates Home' ($process.MainWindowTitle -notmatch 'a\.md|b\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    Key $hwnd 0x09 $true
    if (Expect 'openInTabs=false keeps session parked' ($process.MainWindowTitle -notmatch 'a\.md|b\.md') $process.MainWindowTitle) { $passed++ } else { $failures++ }
    $process.Refresh()
    if (Expect 'openInTabs=false window stays alive' (-not $process.HasExited) 'alive') { $passed++ } else { $failures++ }
} catch {
    "FAIL  openInTabs=false pass: $_"
    $failures++
} finally {
    Close $process
}

"Home startup edges: $passed passed, $failures failed"
exit $failures
