# Ctrl+Shift chords are rows in include/commands.h.
#
#   - a known chord (Ctrl+Shift+S) must still reach its handler;
#   - an unknown chord (Ctrl+Shift+Q) must still be swallowed, so it cannot
#     quit the app through the plain Q binding.
param(
    [string]$Binary = "$PSScriptRoot\..\build\Release\tinta.exe",
    [string]$Output = "$PSScriptRoot\..\out\chord-gate"
)

$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
Remove-Item -Recurse -Force $Output -ErrorAction SilentlyContinue
$appDir = Join-Path $Output 'app'
$repoDir = Join-Path $Output 'repo'
New-Item -ItemType Directory -Force $appDir, $repoDir | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $appDir 'tinta.exe') -Force
Set-Content -Encoding utf8 (Join-Path $repoDir 'a.md') "# A`n`nCtrl+Shift gate."

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class ChordGateWin32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder name, int max);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
    public static string VisibleDialogClass(uint pid, string wanted) {
        string found = "";
        EnumWindows((h, l) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            if (owner != pid || !IsWindowVisible(h)) return true;
            var name = new System.Text.StringBuilder(256);
            GetClassName(h, name, 256);
            if (name.ToString() == wanted) { found = wanted; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@

$repo = "r1|0|Chord probe|$repoDir|"
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
Set-Content -LiteralPath (Join-Path $appDir 'settings.ini') -Value $ini -Encoding utf8

function Wait-Window($process) {
    for ($i = 0; $i -lt 120 -and $process.MainWindowHandle -eq 0 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw 'Tinta did not create a window' }
    Start-Sleep -Milliseconds 900
    return $process.MainWindowHandle
}

function Key([IntPtr]$hwnd, [int]$vk, [switch]$Ctrl, [switch]$Shift) {
    [void][ChordGateWin32]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 150
    if ($Ctrl) { [ChordGateWin32]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero) }
    if ($Shift) { [ChordGateWin32]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero) }
    [ChordGateWin32]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)
    [ChordGateWin32]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero)
    if ($Shift) { [ChordGateWin32]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero) }
    if ($Ctrl) { [ChordGateWin32]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 500
}

function Open-Dialog([int]$processId) {
    return [ChordGateWin32]::VisibleDialogClass([uint32]$processId, '#32770')
}

$failures = 0
$passed = 0
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Key $hwnd 0x0D                                  # open a.md
    $process.Refresh()
    if ($process.MainWindowTitle -notmatch 'a\.md') { throw "a.md did not open ($($process.MainWindowTitle))" }

    Key $hwnd 0x51 -Ctrl -Shift                     # Ctrl+Shift+Q: unknown chord
    $process.Refresh()
    if ($process.HasExited) {
        "FAIL  unknown chord : Ctrl+Shift+Q reached the Q binding and quit"
        $failures++
    } else {
        "PASS  unknown chord : Ctrl+Shift+Q is still swallowed"
        $passed++
    }

    Key $hwnd 0x53 -Ctrl -Shift                     # Ctrl+Shift+S: Save As
    $dialog = Open-Dialog $process.Id
    if ($dialog -eq '#32770') {
        "PASS  known chord : Ctrl+Shift+S opened the Save As dialog"
        $passed++
    } else {
        "FAIL  known chord : Ctrl+Shift+S did not open a dialog"
        $failures++
    }
    Key $hwnd 0x1B                                  # cancel the dialog
    $process.Refresh()
    if ($process.HasExited) {
        "FAIL  dialog : Esc on the Save As dialog closed the app"
        $failures++
    } else {
        "PASS  dialog : Esc only cancelled the dialog"
        $passed++
    }
} catch {
    "FAIL  chord gate pass: $_"
    $failures++
} finally {
    if ($process -and -not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(5000)) { Stop-Process -Id $process.Id -Force }
    }
}

"Ctrl+Shift chord gate: $passed passed, $failures failed"
exit $failures
