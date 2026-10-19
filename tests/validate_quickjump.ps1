# P2b regression: Ctrl+K Quick Jump palette.
#
# Launches portable copies of Tinta against tests/fixtures/quickjump, opens the
# palette with real Ctrl+K input, types queries as WM_CHAR, and checks:
#   - overlay opens/closes and Esc returns to the previous base
#   - Enter opens a result in a tab of this window
#   - folder results navigate the Home tab, and Enter there opens a file
#   - mouse hover/click selects a row
#   - recent-file empty state and index-error state are reachable
#   - normal and narrow widths keep the card on screen
# Screenshots stay in the ignored out/ tree for visual inspection.
param(
    [string]$Binary = "$PSScriptRoot\..\build\Release\tinta.exe",
    [string]$Output = "$PSScriptRoot\..\out\quickjump"
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
Remove-Item -Recurse -Force $Output -ErrorAction SilentlyContinue
$appDir = Join-Path $Output 'app'
$repoDir = Join-Path $Output 'repo'
New-Item -ItemType Directory -Force $appDir, $repoDir | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $appDir 'tinta.exe') -Force
Copy-Item -Path (Join-Path $PSScriptRoot 'fixtures\quickjump\*') -Destination $repoDir -Recurse -Force

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class QuickJumpWin32 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int w, int h, bool repaint);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    // Explicit W entry points: the implicit A variants truncate/mangle
    // Unicode WM_CHAR payloads before they reach the window procedure.
    [DllImport("user32.dll", EntryPoint = "PostMessageW")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@

function Write-Settings([string]$repo) {
    $repoLine = "r1|0|Quick jump probe|$repo|"
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
repo=$repoLine
"@
    Set-Content -LiteralPath (Join-Path $appDir 'settings.ini') -Value $ini -Encoding utf8
}

function Wait-Window($process) {
    for ($i = 0; $i -lt 150 -and $process.MainWindowHandle -eq 0 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw 'Tinta did not create a window' }
    Start-Sleep -Milliseconds 900
    [void][QuickJumpWin32]::SetForegroundWindow($process.MainWindowHandle)
    return $process.MainWindowHandle
}

function Key([IntPtr]$hwnd, [int]$vk) {
    [void][QuickJumpWin32]::SetForegroundWindow($hwnd)
    [void][QuickJumpWin32]::PostMessage($hwnd, 0x0100, [IntPtr]$vk, [IntPtr]0)
    [void][QuickJumpWin32]::PostMessage($hwnd, 0x0101, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 550
}

function Open-Palette([IntPtr]$hwnd) {
    [void][QuickJumpWin32]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 150
    [QuickJumpWin32]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)  # Ctrl down
    Start-Sleep -Milliseconds 70
    [QuickJumpWin32]::keybd_event(0x4B, 0, 0, [UIntPtr]::Zero)  # K down
    Start-Sleep -Milliseconds 50
    [QuickJumpWin32]::keybd_event(0x4B, 0, 2, [UIntPtr]::Zero)  # K up
    Start-Sleep -Milliseconds 50
    [QuickJumpWin32]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)  # Ctrl up
    Start-Sleep -Milliseconds 650
}

function TypeText([IntPtr]$hwnd, [string]$text) {
    foreach ($ch in $text.ToCharArray()) {
        [void][QuickJumpWin32]::PostMessage($hwnd, 0x0102, [IntPtr][int][char]$ch, [IntPtr]0)
    }
    Start-Sleep -Milliseconds 550
}

# Real keyboard path: WM_KEYDOWN -> TranslateMessage -> WM_CHAR. This is the
# path a user actually exercises; posting WM_CHAR directly would hide the
# "palette swallows printable keys" regression. Posting WM_KEYDOWN (instead of
# injecting physical keys) keeps the test independent of host focus/key state.
function TypeKeys([IntPtr]$hwnd, [string]$text) {
    $scan = @{
        'Q'=0x10; 'W'=0x11; 'E'=0x12; 'R'=0x13; 'T'=0x14; 'Y'=0x15; 'U'=0x16;
        'I'=0x17; 'O'=0x18; 'P'=0x19; 'A'=0x1E; 'S'=0x1F; 'D'=0x20; 'F'=0x21;
        'G'=0x22; 'H'=0x23; 'J'=0x24; 'K'=0x25; 'L'=0x26; 'Z'=0x2C; 'X'=0x2D;
        'C'=0x2E; 'V'=0x2F; 'B'=0x30; 'N'=0x31; 'M'=0x32; ' '=0x39
    }
    foreach ($ch in $text.ToCharArray()) {
        $key = [char]::ToUpperInvariant($ch)
        if (-not $scan.ContainsKey([string]$key)) { continue }
        $vk = if ($ch -eq ' ') { 0x20 } else { [int]$key }
        $s = [int]$scan[[string]$key]
        $down = [IntPtr](1 -bor ($s -shl 16))
        $up = [IntPtr](1 -bor ($s -shl 16) -bor 0xC0000000)
        [void][QuickJumpWin32]::PostMessage($hwnd, 0x0100, [IntPtr]$vk, $down)
        Start-Sleep -Milliseconds 20
        [void][QuickJumpWin32]::PostMessage($hwnd, 0x0101, [IntPtr]$vk, $up)
        Start-Sleep -Milliseconds 30
    }
    Start-Sleep -Milliseconds 550
}

function Capture([IntPtr]$hwnd, [string]$name) {
    $rect = New-Object QuickJumpWin32+RECT
    [void][QuickJumpWin32]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    if ($w -lt 200 -or $h -lt 200) { throw "bad window size for $name" }
    $bitmap = New-Object System.Drawing.Bitmap $w, $h
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    [void][QuickJumpWin32]::PrintWindow($hwnd, $hdc, 2)
    $graphics.ReleaseHdc($hdc)
    $graphics.Dispose()
    $path = Join-Path $Output $name
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    if (!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -lt 20000) {
        throw "empty capture: $name"
    }
    return $path
}

function Read-Pixels([string]$path) {
    $bitmap = [System.Drawing.Bitmap]::FromFile($path)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $bitmap.Width, $bitmap.Height
    $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                             [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $data.Height)
    [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $result = [pscustomobject]@{
        Bytes = $bytes; Stride = $data.Stride; Width = $bitmap.Width; Height = $bitmap.Height
    }
    $bitmap.UnlockBits($data)
    $bitmap.Dispose()
    return $result
}

function Diff-Ratio([string]$a, [string]$b) {
    $pa = Read-Pixels $a
    $pb = Read-Pixels $b
    if ($pa.Width -ne $pb.Width -or $pa.Height -ne $pb.Height) { return 1.0 }
    $diff = 0
    $total = 0
    for ($y = 0; $y -lt $pa.Height; $y += 4) {
        for ($x = 0; $x -lt $pa.Width; $x += 4) {
            $ia = $y * $pa.Stride + $x * 4
            $ib = $y * $pb.Stride + $x * 4
            $d = [Math]::Abs($pa.Bytes[$ia] - $pb.Bytes[$ib]) +
                 [Math]::Abs($pa.Bytes[$ia + 1] - $pb.Bytes[$ib + 1]) +
                 [Math]::Abs($pa.Bytes[$ia + 2] - $pb.Bytes[$ib + 2])
            if ($d -gt 24) { $diff++ }
            $total++
        }
    }
    if ($total -eq 0) { return 1.0 }
    return [double]$diff / [double]$total
}

function Wait-Title($process, [string]$needle, [int]$timeoutMs = 4000) {
    for ($i = 0; $i -lt [Math]::Ceiling($timeoutMs / 100); $i++) {
        $process.Refresh()
        if ($process.HasExited) { return $false }
        if ($process.MainWindowTitle -like "*$needle*") { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Focus-Row([IntPtr]$hwnd, [int]$row) {
    # Match renderQuickJump's 96-DPI geometry: 640-wide card, 470 high,
    # 18 padding, 42 field, 8 gap, 20 recent header, 48 rows.
    $client = New-Object QuickJumpWin32+RECT
    [void][QuickJumpWin32]::GetClientRect($hwnd, [ref]$client)
    $w = $client.Right - $client.Left
    $h = $client.Bottom - $client.Top
    $cardW = [Math]::Min(640, $w - 48)
    $cardH = [Math]::Min(470, $h - 40 - 56)
    $cardX = ($w - $cardW) / 2
    $cardY = 40 + [Math]::Max(28, ($h - 40 - $cardH) / 2)
    $listTop = $cardY + 18 + 42 + 8
    $rowsTop = $listTop + 20
    $x = [int]($cardX + 160)
    $y = [int]($rowsTop + 48 * $row + 24)
    $lp = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [void][QuickJumpWin32]::PostMessage($hwnd, 0x0200, [IntPtr]0, $lp)       # hover
    Start-Sleep -Milliseconds 250
    [void][QuickJumpWin32]::PostMessage($hwnd, 0x0201, [IntPtr]1, $lp)       # click
    [void][QuickJumpWin32]::PostMessage($hwnd, 0x0202, [IntPtr]0, $lp)
    Start-Sleep -Milliseconds 700
}

function Close($process) {
    if ($process -and -not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(5000)) { Stop-Process -Id $process.Id -Force }
    }
}

function Expect([string]$name, [bool]$ok, [string]$detail) {
    if ($ok) { "PASS  $name ($detail)"; return $true }
    "FAIL  $name ($detail)"; return $false
}

$failures = 0
$passed = 0
$results = @()
function Check([string]$name, [bool]$ok, [string]$detail) {
    if (Expect $name $ok $detail) { $script:passed++ } else { $script:failures++ }
}

# Recent order: README.md is the newest row (first click target).
(Get-Item (Join-Path $repoDir 'README.md')).LastWriteTime = Get-Date
(Get-Item (Join-Path $repoDir 'notes\alpha.md')).LastWriteTime = (Get-Date).AddMinutes(-5)
(Get-Item (Join-Path $repoDir 'notes\beta.md')).LastWriteTime = (Get-Date).AddMinutes(-10)
(Get-Item (Join-Path $repoDir 'deep\gamma.md')).LastWriteTime = (Get-Date).AddMinutes(-15)

Write-Settings $repoDir
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru -WorkingDirectory $appDir
try {
    $hwnd = Wait-Window $process
    $homeShot = Capture $hwnd '01-home.png'

    Open-Palette $hwnd
    $palette = Capture $hwnd '02-palette-recent.png'
    $ratio = Diff-Ratio $homeShot $palette
    Check 'palette opens from Home' ($ratio -gt 0.05) ("screen diff {0:P1}" -f $ratio)

    Key $hwnd 0x1B                                   # Esc closes, Home unchanged
    $closed = Capture $hwnd '03-palette-closed.png'
    $closedRatio = Diff-Ratio $homeShot $closed
    Check 'Esc restores the Home base' ($closedRatio -lt 0.02 -and -not $process.HasExited) ("screen diff {0:P1}" -f $closedRatio)

    Open-Palette $hwnd
    TypeKeys $hwnd 'alpha notes'
    $queryShot = Capture $hwnd '04-palette-query.png'
    Key $hwnd 0x0D                                   # Enter opens alpha.md
    Check 'Enter opens the selected file' (Wait-Title $process 'alpha.md' 4000) $process.MainWindowTitle
    Capture $hwnd '05-reader-alpha.png' | Out-Null

    # Esc from the reader goes back to the reader, not Home; palette Esc is
    # checked before that transition.
    $readerBefore = Capture $hwnd '06-reader-before-palette.png'
    Open-Palette $hwnd
    $readerPalette = Capture $hwnd '07-reader-palette.png'
    $readerRatio = Diff-Ratio $readerBefore $readerPalette
    Key $hwnd 0x1B
    $readerAfter = Capture $hwnd '08-reader-after-esc.png'
    $readerAfterRatio = Diff-Ratio $readerBefore $readerAfter
    Check 'palette opens over the reader' ($readerRatio -gt 0.05) ("screen diff {0:P1}" -f $readerRatio)
    Check 'Esc returns to the reader' ($readerAfterRatio -lt 0.02 -and (Wait-Title $process 'alpha.md' 1000)) ("screen diff {0:P1}" -f $readerAfterRatio)

    Key $hwnd 0x1B                                   # reader Esc -> Home
    Start-Sleep -Milliseconds 850
    $process.Refresh()
    $homeAgain = Capture $hwnd '09-home-again.png'
    Check 'reader Esc returns to Home' ($process.MainWindowTitle -eq 'Tinta') $process.MainWindowTitle

    # A long file name must stay inside the card (trimmed with an ellipsis).
    Open-Palette $hwnd
    $longOpen = Capture $hwnd '16a-palette-long-open.png'
    TypeKeys $hwnd 'trimmed'
    $longShot = Capture $hwnd '16-palette-long-name.png'
    $longDiff = Diff-Ratio $longOpen $longShot
    Check 'typing updates the result list' ($longDiff -gt 0.01) ("screen diff {0:P1}" -f $longDiff)
    Key $hwnd 0x1B
    Start-Sleep -Milliseconds 500

    # Folder jump: Enter on a folder row navigates Home, then Home's Enter
    # opens the only file in that folder.
    Open-Palette $hwnd
    TypeKeys $hwnd 'archive'
    Capture $hwnd '10-palette-folder.png' | Out-Null
    Key $hwnd 0x0D
    $process.Refresh()
    $homeArchive = Capture $hwnd '11-home-archive.png'
    $folderRatio = Diff-Ratio $homeAgain $homeArchive
    Check 'folder result navigates Home' ($folderRatio -gt 0.02 -and $process.MainWindowTitle -eq 'Tinta') ("screen diff {0:P1}" -f $folderRatio)
    Key $hwnd 0x0D
    Check 'Enter in the folder opens its file' (Wait-Title $process 'gamma-old.md' 4000) $process.MainWindowTitle

    # Mouse: the first recent row is README.md; click it after returning Home.
    Key $hwnd 0x1B                                   # reader Esc -> Home
    Start-Sleep -Milliseconds 850
    Open-Palette $hwnd
    Focus-Row $hwnd 0
    Check 'mouse click opens a row' (Wait-Title $process 'README.md' 4000) $process.MainWindowTitle
    Capture $hwnd '12-reader-mouse.png' | Out-Null

    # Edit mode + CJK query: Ctrl+K is global, Esc returns to editing, and
    # WM_CHAR text reaches the field while the editor owns the base.
    Key $hwnd 0x45                                   # E enters edit mode
    Start-Sleep -Milliseconds 800
    $editBefore = Capture $hwnd '14-edit-before-palette.png'
    Open-Palette $hwnd
    $editPalette = Capture $hwnd '14a-edit-palette.png'
    $editRatio = Diff-Ratio $editBefore $editPalette
    Check 'palette opens over edit mode' ($editRatio -gt 0.05) ("screen diff {0:P1}" -f $editRatio)
    Key $hwnd 0x1B
    $editAfter = Capture $hwnd '14b-edit-after-esc.png'
    $editAfterRatio = Diff-Ratio $editBefore $editAfter
    Check 'Esc returns to edit mode' ($editAfterRatio -lt 0.02 -and (Wait-Title $process 'README.md' 1000)) ("screen diff {0:P1}" -f $editAfterRatio)

    Open-Palette $hwnd
    TypeText $hwnd '测试'
    Capture $hwnd '14-palette-cjk-edit.png' | Out-Null
    Key $hwnd 0x0D
    Check 'palette opens in edit mode and accepts CJK' (Wait-Title $process '测试笔记.md' 4000) $process.MainWindowTitle
    Capture $hwnd '15-reader-cjk.png' | Out-Null

    # Narrow window: card must remain inside the client area and still accept
    # a query. A capture is the evidence; size is asserted here.
    Key $hwnd 0x1B                                   # reader -> Home
    Start-Sleep -Milliseconds 700
    [void][QuickJumpWin32]::MoveWindow($hwnd, 60, 60, 560, 700, $true)
    Start-Sleep -Milliseconds 900
    Open-Palette $hwnd
    TypeKeys $hwnd 'gamma'
    $narrow = Capture $hwnd '13-palette-narrow.png'
    $np = Read-Pixels $narrow
    Check 'narrow palette renders' ($np.Width -ge 540 -and $np.Height -ge 680) ("{0}x{1}" -f $np.Width, $np.Height)
    Key $hwnd 0x1B
} catch {
    "FAIL  main pass: $_"
    $failures++
} finally {
    Close $process
}

# Recent-files empty state: the repository has no supported documents and no
# folders, so the index is ready but has nothing to list.
$emptyRepo = Join-Path $Output 'repo-empty'
New-Item -ItemType Directory -Force $emptyRepo | Out-Null
$image = Join-Path $PSScriptRoot 'fixtures\preview-follow-img-01.png'
if (Test-Path $image) { Copy-Item -LiteralPath $image -Destination (Join-Path $emptyRepo 'only-image.png') -Force }
Write-Settings $emptyRepo
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru -WorkingDirectory $appDir
try {
    $hwnd = Wait-Window $process
    $base = Capture $hwnd '20-empty-home.png'
    Open-Palette $hwnd
    $shot = Capture $hwnd '21-palette-recent-empty.png'
    $ratio = Diff-Ratio $base $shot
    Check 'recent empty state opens' ($ratio -gt 0.05 -and -not $process.HasExited) ("screen diff {0:P1}" -f $ratio)
    Key $hwnd 0x1B
    Check 'recent empty state closes' (-not $process.HasExited) 'process alive'
} catch {
    "FAIL  recent-empty pass: $_"
    $failures++
} finally {
    Close $process
}

# Index error: remove the repository after launch, then open the palette.
$errorRepo = Join-Path $Output 'repo-error'
New-Item -ItemType Directory -Force $errorRepo | Out-Null
Set-Content -Encoding utf8 (Join-Path $errorRepo 'a.md') "# A`n`nIndex error probe."
Write-Settings $errorRepo
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru -WorkingDirectory $appDir
try {
    $hwnd = Wait-Window $process
    Start-Sleep -Milliseconds 700
    Rename-Item -LiteralPath $errorRepo -NewName 'repo-error-gone'
    Open-Palette $hwnd
    Start-Sleep -Milliseconds 900
    $shot = Capture $hwnd '30-palette-index-error.png'
    Check 'index error state opens' ((Get-Item $shot).Length -gt 20000 -and -not $process.HasExited) 'capture written'
    Key $hwnd 0x1B
    Check 'index error state closes' (-not $process.HasExited) 'process alive'
} catch {
    "FAIL  index-error pass: $_"
    $failures++
} finally {
    Close $process
}

# Indexing state: a large tree makes the first scan visible long enough to
# capture. This is evidence, not a timing-sensitive pass/fail check.
$bigRepo = Join-Path $Output 'repo-big'
New-Item -ItemType Directory -Force $bigRepo | Out-Null
for ($i = 1; $i -le 2500; $i++) {
    [IO.File]::WriteAllText((Join-Path $bigRepo ('note-{0:D4}.md' -f $i)), "# Note $i`n`nbody`n")
}
Write-Settings $bigRepo
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru -WorkingDirectory $appDir
try {
    $hwnd = Wait-Window $process
    [void][QuickJumpWin32]::SetForegroundWindow($hwnd)
    [QuickJumpWin32]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    [QuickJumpWin32]::keybd_event(0x4B, 0, 0, [UIntPtr]::Zero)
    [QuickJumpWin32]::keybd_event(0x4B, 0, 2, [UIntPtr]::Zero)
    [QuickJumpWin32]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
    $shot = Capture $hwnd '40-palette-indexing.png'
    Check 'large-tree palette renders' ((Get-Item $shot).Length -gt 20000 -and -not $process.HasExited) 'capture written'
    Key $hwnd 0x1B
} catch {
    "FAIL  indexing pass: $_"
    $failures++
} finally {
    Close $process
}

"Quick Jump validation: $passed passed, $failures failed"
exit $failures
