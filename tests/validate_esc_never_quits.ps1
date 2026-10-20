# P0 regression: Esc must never close the application.
#
# Exercises the three bottom-of-stack cases:
#   - Home with a repository
#   - Reader showing a document
#   - Picker with no repositories
#
# A process that is still alive after Esc is the assertion. The portable copy
# keeps the test away from the real settings.ini.
param(
    [string]$Binary = "$PSScriptRoot\..\build\Release\tinta.exe",
    [string]$Output = "$PSScriptRoot\..\out\esc-never-quits"
)
$ErrorActionPreference = 'Stop'
$Output = [IO.Path]::GetFullPath($Output)
Remove-Item -Recurse -Force $Output -ErrorAction SilentlyContinue
$appDir = Join-Path $Output 'app'
$repoDir = Join-Path $Output 'repo'
New-Item -ItemType Directory -Force $appDir, $repoDir | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $appDir 'tinta.exe') -Force
Set-Content -Encoding utf8 (Join-Path $repoDir 'a.md') "# A`n`nEsc should not close the reader."

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class EscNeverQuitsWin32 {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
"@

function Write-Settings([bool]$withRepo) {
    $repos = if ($withRepo) {
        $repo = "r1|0|Esc probe|$repoDir|"
        "[Repos]`nstartupMode=0`nrepoCurrent=r1`nrepo=$repo`n"
    } else { "" }
    $ini = @"
[Settings]
themeIndex=0
followSystemTheme=0
hasAskedFileAssociation=1
openInTabs=0
checkUpdates=0
language=en

$repos
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
    [void][EscNeverQuitsWin32]::PostMessage($hwnd, 0x0100, [IntPtr]$vk, [IntPtr]0)
    [void][EscNeverQuitsWin32]::PostMessage($hwnd, 0x0101, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 700
}

function Expect-Alive($process, [string]$name) {
    $process.Refresh()
    if ($process.HasExited) {
        "FAIL  $name : process exited after Esc"
        return $false
    }
    "PASS  $name : still alive"
    return $true
}

function Expect-Exited($process, [string]$name) {
    for ($i = 0; $i -lt 40 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    if ($process.HasExited) {
        "PASS  $name : explicit quit still works"
        return $true
    }
    "FAIL  $name : explicit quit was blocked"
    return $false
}

function Close($process) {
    if ($process -and -not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(5000)) { Stop-Process -Id $process.Id -Force }
    }
}

$failures = 0
$passed = 0

# Home + reader
Write-Settings $true
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Key $hwnd 0x1B                    # Home, no filter
    if (Expect-Alive $process 'Home') { $passed++ } else { $failures++ }
    Key $hwnd 0x0D                    # open a.md
    if ($process.MainWindowTitle -notmatch 'a\.md') {
        "FAIL  reader : a.md did not open ($($process.MainWindowTitle))"
        $failures++
    } else {
        Key $hwnd 0x1B                # reader
        if (Expect-Alive $process 'Reader') { $passed++ } else { $failures++ }
    }
} catch {
    "FAIL  Home/reader pass: $_"
    $failures++
} finally {
    Close $process
}

# Reader overlays: Esc closes one layer, then climbs to Home. Opening a panel
# must not turn the first Esc into "quit" or into "jump straight Home".
Write-Settings $true
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Key $hwnd 0x0D                    # open a.md
    Key $hwnd 0x46                    # F opens the search panel
    Key $hwnd 0x1B                    # first Esc: close search only
    $process.Refresh()
    if (Expect-Alive $process 'Reader search, first Esc') { $passed++ } else { $failures++ }
    if ($process.MainWindowTitle -notmatch 'a\.md') {
        "FAIL  reader overlay : first Esc left the reader ($($process.MainWindowTitle))"
        $failures++
    } else {
        "PASS  reader overlay : search closed, reader stayed"
        $passed++
    }
    Key $hwnd 0x1B                    # second Esc: back to Home
    $process.Refresh()
    if ($process.MainWindowTitle -match 'a\.md') {
        "FAIL  reader overlay : second Esc did not reach Home ($($process.MainWindowTitle))"
        $failures++
    } else {
        "PASS  reader overlay : second Esc reached Home"
        $passed++
    }
} catch {
    "FAIL  reader overlay pass: $_"
    $failures++
} finally {
    Close $process
}

# Picker with no repositories
Write-Settings $false
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Key $hwnd 0x1B                    # picker, no root
    if (Expect-Alive $process 'Picker') { $passed++ } else { $failures++ }
} catch {
    "FAIL  Picker pass: $_"
    $failures++
} finally {
    Close $process
}

# Explicit quit must still work: removing the Esc fallback must not remove Q.
Write-Settings $true
$process = Start-Process -FilePath (Join-Path $appDir 'tinta.exe') -PassThru
try {
    $hwnd = Wait-Window $process
    Key $hwnd 0x51                    # Q
    if (Expect-Exited $process 'Home Q') { $passed++ } else { $failures++ }
} catch {
    "FAIL  Quit pass: $_"
    $failures++
} finally {
    Close $process
}

"Esc never quits: $passed passed, $failures failed"
exit $failures
