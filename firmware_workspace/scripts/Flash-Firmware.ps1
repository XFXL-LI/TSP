[CmdletBinding()]
param(
    [string]$Port,
    [string]$ConfirmFlash,
    [string]$ReleasePath,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
if (-not $ReleasePath) {
    $ReleasePath = Join-Path $workspaceRoot 'releases\2.0.4\2026-08-10_hw-verified'
}
$ReleasePath = [IO.Path]::GetFullPath($ReleasePath)
$allowedReleaseRoot = [IO.Path]::GetFullPath((Join-Path $workspaceRoot 'releases'))
$esptool = Join-Path $workspaceRoot 'toolchain\esptool.exe'

if (-not $ReleasePath.StartsWith($allowedReleaseRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Only packages under the releases directory may be flashed: $ReleasePath"
}
if (-not (Test-Path -LiteralPath $esptool)) {
    throw "esptool not found: $esptool"
}

$manifestPath = Join-Path $ReleasePath 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Release package has no manifest.json: $ReleasePath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($name in @('bootloader.bin', 'partitions.bin', 'boot_app0.bin', 'firmware.bin')) {
    $path = Join-Path $ReleasePath $name
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Release package is missing: $name"
    }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    $expected = $manifest.files.$name.sha256
    if ($actual -ne $expected) {
        throw "SHA-256 verification failed: $name"
    }
}

Write-Host "Release validation passed: $ReleasePath"
if ($ValidateOnly) {
    Write-Host 'Validation-only mode; no MCU flashing was performed.'
    return
}

if ($ConfirmFlash -cne 'FLASH') {
    throw 'Exact confirmation word FLASH was not provided; flashing refused.'
}
if ($Port -notmatch '^COM\d+$') {
    throw "Invalid serial port: $Port"
}

Write-Host "Target port: $Port"
Write-Host 'Mode: four segments; no full-chip erase; FFat is not written.'

Push-Location $ReleasePath
try {
    & $esptool --chip esp32s3 --port $Port --baud 921600 --before default-reset --after hard-reset `
        write-flash -z --flash-mode dio --flash-freq 80m --flash-size 16MB `
        0x0 bootloader.bin `
        0x8000 partitions.bin `
        0xe000 boot_app0.bin `
        0x10000 firmware.bin
    if ($LASTEXITCODE -ne 0) {
        throw "Flashing failed; esptool exit code: $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

Write-Host 'Flash command completed; verify write hashes and the device boot log.'
