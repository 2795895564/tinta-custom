# Signs local Tinta builds with a per-user development certificate.
#
# This is for a development machine only. It creates (or reuses) a self-signed
# code-signing certificate in CurrentUser\My. Pass -Trust the first time to
# also import it into CurrentUser\Root and CurrentUser\TrustedPublisher.
#
# SmartScreen reputation is separate: a self-signed certificate removes the
# "unknown publisher" warning, but Windows may still warn about a new, low
# reputation binary. For local dev, turning off "Check apps and files" in
# Windows Security is the reliable option.
#
# Usage (normal PowerShell, no administrator):
#   .\tools\sign-dev.ps1 -Trust
#   .\tools\sign-dev.ps1
#   .\tools\sign-dev.ps1 -Exe "D:\Tinta\tinta.exe"
#
# A running Tinta locks its exe; that file is skipped with a message.

param(
    [string[]]$Exe = @(),
    [string]$Subject = "CN=Tinta Dev",
    [switch]$Trust
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Exe.Count -eq 0) {
    $Exe = @(
        (Join-Path $root 'build\Release\tinta.exe'),
        (Join-Path $root 'build\Release\tinta-inline.exe')
    )
}

$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq $Subject } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $cert) {
    Write-Host "Creating development certificate: $Subject"
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $Subject `
        -KeyUsage DigitalSignature `
        -FriendlyName 'Tinta development signing' `
        -CertStoreLocation Cert:\CurrentUser\My `
        -NotAfter (Get-Date).AddYears(3)
}

if ($Trust) {
    $cer = Join-Path $env:TEMP 'tinta-dev-signing.cer'
    Export-Certificate -Cert $cert -FilePath $cer -Force | Out-Null
    Import-Certificate -CertStoreLocation Cert:\CurrentUser\Root -FilePath $cer | Out-Null
    Import-Certificate -CertStoreLocation Cert:\CurrentUser\TrustedPublisher -FilePath $cer | Out-Null
    Write-Host "Trusted $Subject for the current user."
}

$signed = 0
foreach ($path in $Exe) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "skip (missing): $path"
        continue
    }
    try {
        $sig = Set-AuthenticodeSignature -FilePath $path -Certificate $cert
        Write-Host ("{0} -> {1}" -f $path, $sig.Status)
        $signed++
    } catch {
        Write-Host "skip (in use or inaccessible): $path"
        Write-Host "  close that Tinta instance, or sign the copy you actually run"
    }
}

Write-Host "Signed $signed file(s)."
Write-Host "If SmartScreen still blocks, use Windows Security > App & browser control > SmartScreen and turn off 'Check apps and files' for this dev machine."
