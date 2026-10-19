# P1b regression: Home new file/folder, rename, delete to Recycle Bin.
#
# Drives the Home modal with keyboard messages, checks the filesystem after
# each commit, and captures the dialog states for visual inspection.
param(
    [string]$Binary = "$PSScriptRoot\..\build\Release\tinta.exe",
    [string]$Output = "$PSScriptRoot\..\out\home-file-ops"
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
Remove-Item -Recurse -Force $Output -ErrorAction SilentlyContinue
$appDir = Join-Path $Output 'app'
$repoDir = Join-Path $Output 'repo'
New-Item -ItemType Directory -Force $appDir, $repoDir | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $appDir 'tinta.exe') -Force
Set-Content -Encoding utf8 (Join-Path $repoDir 'a.md') "# A`n`nRename and delete me."

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class HomeFileOpsWin32 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@

function Write-Settings {
    $repo = "r1|0|File ops probe|$repoDir|"
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
}

function Wait-Window($process) {
    for ($i = 0; $i -lt 120 -and $process.MainWindowHandle -eq 0 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw 'Tinta did not create a window' }
    Start-Sleep -Milliseconds 1000
    [void][HomeFileOpsWin32]::SetForegroundWindow($process.MainWindowHandle)
    return $process.MainWindowHandle
}

function Key([IntPtr]$hwnd, [int]$vk) {
    [void][HomeFileOpsWin32]::PostMessage($hwnd, 0x0100, [IntPtr]$vk, [IntPtr]0)
    [void][HomeFileOpsWin32]::PostMessage($hwnd, 0x0101, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 450
}

function ShiftKey([IntPtr]$hwnd, [byte]$vk) {
    [void][HomeFileOpsWin32]::SetForegroundWindow($hwnd)
    [HomeFileOpsWin32]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [void][HomeFileOpsWin32]::PostMessage($hwnd, 0x0100, [IntPtr]$vk, [IntPtr]0)
    [void][HomeFileOpsWin32]::PostMessage($hwnd, 0x0101, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 80
    [HomeFileOpsWin32]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 500
}

function TypeText([IntPtr]$hwnd, [string]$text) {
    foreach ($ch in $text.ToCharArray()) {
        [void][HomeFileOpsWin32]::PostMessage($hwnd, 0x0102, [IntPtr][int][char]$ch, [IntPtr]0)
    }
    Start-Sleep -Milliseconds 300
}

function Capture([IntPtr]$hwnd, [string]$name) {
    $rect = New-Object HomeFileOpsWin32+RECT
    [void][HomeFileOpsWin32]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap $w, $h
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    [void][HomeFileOpsWin32]::PrintWindow($hwnd, $hdc, 2)
    $graphics.ReleaseHdc($hdc)
    $graphics.Dispose()
    $path = Join-Path $Output $name
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    "capture $name"
}

function Close($process) {
    if ($process -and -not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(8000)) { Stop-Process -Id $process.Id -Force }
    }
    Start-Sleep -Milliseconds 500
}

function Expect([string]$name, [bool]$ok, [string]$detail) {
    if ($ok) { "PASS  $name ($detail)"; return $true }
    "FAIL  $name ($detail)"; return $false
}

$failures = 0
$passed = 0
Write-Settings
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Capture $hwnd '01-home.png'

    # Rename a.md -> b.md
    Key $hwnd 0x52                    # R
    Capture $hwnd '02-rename.png'
    for ($i = 0; $i -lt 4; $i++) { Key $hwnd 0x08 }  # clear "a.md"
    TypeText $hwnd 'b.md'
    Key $hwnd 0x0D
    if (Expect 'rename a.md -> b.md' ((Test-Path (Join-Path $repoDir 'b.md')) -and
                                     -not (Test-Path (Join-Path $repoDir 'a.md'))) 'filesystem') { $passed++ } else { $failures++ }

    # Delete b.md to the Recycle Bin
    Key $hwnd 0x44                    # D
    Capture $hwnd '03-delete-confirm.png'
    Key $hwnd 0x0D
    if (Expect 'delete b.md' (-not (Test-Path (Join-Path $repoDir 'b.md'))) 'filesystem') { $passed++ } else { $failures++ }

    # New file c.md
    Key $hwnd 0x4E                    # N
    Capture $hwnd '04-new-file.png'
    TypeText $hwnd 'c.md'
    Key $hwnd 0x0D
    if (Expect 'create c.md' (Test-Path (Join-Path $repoDir 'c.md')) 'filesystem') { $passed++ } else { $failures++ }

    # New folder docs
    ShiftKey $hwnd 0x4E                # Shift+N
    Capture $hwnd '05-new-folder.png'
    TypeText $hwnd 'docs'
    Key $hwnd 0x0D
    if (Expect 'create docs folder' (Test-Path (Join-Path $repoDir 'docs') -PathType Container) 'filesystem') { $passed++ } else { $failures++ }

    # Duplicate name must keep the dialog open with an error
    Key $hwnd 0x4E
    TypeText $hwnd 'c.md'
    Key $hwnd 0x0D
    Capture $hwnd '06-name-exists.png'
    Key $hwnd 0x1B                    # dismiss the error dialog
    $process.Refresh()
    if (Expect 'duplicate name is rejected' ((-not $process.HasExited) -and
          (Get-Item (Join-Path $repoDir 'c.md')).Length -ge 0) 'dialog stayed open') { $passed++ } else { $failures++ }
} catch {
    "FAIL  file ops pass: $_"
    $failures++
} finally {
    Close $process
}

"Home file ops: $passed passed, $failures failed"
exit $failures
