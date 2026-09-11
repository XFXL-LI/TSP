[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Label,
    [string]$Version = '2.0.6',
    [string]$BuildPath
)

$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$allowedBuildRoot = [IO.Path]::GetFullPath((Join-Path $workspaceRoot 'build'))
if (-not $BuildPath) {
    $BuildPath = Join-Path $allowedBuildRoot 'current'
}
$buildPath = [IO.Path]::GetFullPath($BuildPath)
if (-not $buildPath.StartsWith($allowedBuildRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Build directory must be under $allowedBuildRoot; refused: $buildPath"
}
$releasePath = Join-Path $workspaceRoot ("releases\{0}\{1}" -f $Version, $Label)

if ($Label -notmatch '^[A-Za-z0-9._-]+$') {
    throw 'Label may contain only letters, digits, dots, underscores, and hyphens.'
}
if (Test-Path -LiteralPath $releasePath) {
    throw "Release directory already exists; refusing overwrite: $releasePath"
}

$mapping = [ordered]@{
    'TSP.ino.bin' = 'firmware.bin'
    'TSP.ino.bootloader.bin' = 'bootloader.bin'
    'TSP.ino.partitions.bin' = 'partitions.bin'
    'boot_app0.bin' = 'boot_app0.bin'
}
foreach ($sourceName in $mapping.Keys) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildPath $sourceName))) {
        throw "Current build is missing $sourceName; run Build-Firmware.ps1 first."
    }
}

New-Item -ItemType Directory -Path $releasePath | Out-Null
foreach ($sourceName in $mapping.Keys) {
    Copy-Item -LiteralPath (Join-Path $buildPath $sourceName) -Destination (Join-Path $releasePath $mapping[$sourceName])
}

@'
--flash-mode dio --flash-freq 80m --flash-size 16MB
0x0 bootloader.bin
0x8000 partitions.bin
0xe000 boot_app0.bin
0x10000 firmware.bin
'@ | Set-Content -LiteralPath (Join-Path $releasePath 'flash_args.txt') -Encoding ASCII

$files = [ordered]@{}
$sumLines = foreach ($name in @('bootloader.bin', 'partitions.bin', 'boot_app0.bin', 'firmware.bin')) {
    $path = Join-Path $releasePath $name
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    $files[$name] = [ordered]@{ bytes = (Get-Item -LiteralPath $path).Length; sha256 = $hash }
    "$hash  $name"
}
$sumLines | Set-Content -LiteralPath (Join-Path $releasePath 'SHA256SUMS.txt') -Encoding ASCII

$buildManifestPath = Join-Path $buildPath 'build-manifest.json'
$buildManifest = if (Test-Path -LiteralPath $buildManifestPath) {
    Get-Content -LiteralPath $buildManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
} else { $null }
$manifest = [ordered]@{
    firmwareVersion = $Version
    label = $Label
    status = 'packaged-not-hardware-verified'
    packagedAt = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ssK')
    sourcePath = if ($buildManifest) { $buildManifest.sourcePath } else { $null }
    sourceFingerprintSha256 = if ($buildManifest) { $buildManifest.sourceFingerprintSha256 } else { $null }
    esp32Core = '3.3.7'
    flashLayout = [ordered]@{
        '0x0' = 'bootloader.bin'
        '0x8000' = 'partitions.bin'
        '0xe000' = 'boot_app0.bin'
        '0x10000' = 'firmware.bin'
    }
    preservesFFat = $true
    files = $files
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $releasePath 'manifest.json') -Encoding UTF8

Write-Host "Release package created: $releasePath"
Write-Host 'Status is not hardware-verified; update it only after flashing and testing.'
