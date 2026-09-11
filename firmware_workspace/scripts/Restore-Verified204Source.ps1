[CmdletBinding()]
param(
    [string]$AuditBuildPath,
    [string]$ReleasePath,
    [string]$DestinationPath
)

$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $workspaceRoot

if (-not $AuditBuildPath) {
    $AuditBuildPath = Join-Path $workspaceRoot `
        'archive\legacy-builds\current-source\.build_204_audit_20260810'
}
if (-not $ReleasePath) {
    $ReleasePath = Join-Path $workspaceRoot `
        'releases\2.0.4\2026-08-10_hw-verified'
}
if (-not $DestinationPath) {
    $DestinationPath = Join-Path $workspaceRoot `
        'sources\Firmware_2.0.4_hw-verified\TSP'
}

$AuditBuildPath = [IO.Path]::GetFullPath($AuditBuildPath)
$ReleasePath = [IO.Path]::GetFullPath($ReleasePath)
$DestinationPath = [IO.Path]::GetFullPath($DestinationPath)
$allowedDestinationRoot = [IO.Path]::GetFullPath(
    (Join-Path $workspaceRoot 'sources'))
$auditSketchPath = Join-Path $AuditBuildPath 'sketch'
$auditBinPath = Join-Path $AuditBuildPath 'TSP.ino.bin'
$releaseManifestPath = Join-Path $ReleasePath 'manifest.json'

if (-not $DestinationPath.StartsWith(
        $allowedDestinationRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destination must be under $allowedDestinationRoot"
}
if (Test-Path -LiteralPath $DestinationPath) {
    throw "Destination already exists; refusing to overwrite: $DestinationPath"
}
if (-not (Test-Path -LiteralPath $auditSketchPath)) {
    throw "Audit sketch directory not found: $auditSketchPath"
}
if (-not (Test-Path -LiteralPath $releaseManifestPath)) {
    throw "Release manifest not found: $releaseManifestPath"
}

$releaseManifest = Get-Content -LiteralPath $releaseManifestPath -Raw `
    -Encoding UTF8 | ConvertFrom-Json
$auditBinHash = (Get-FileHash -Algorithm SHA256 `
    -LiteralPath $auditBinPath).Hash
$releaseBinHash = $releaseManifest.files.'firmware.bin'.sha256
if ($auditBinHash -ne $releaseBinHash) {
    throw "Audit build BIN does not match the verified release manifest"
}

New-Item -ItemType Directory -Path $DestinationPath | Out-Null

# Arduino's build cache retains an exact copy of each source file with one
# generated #line directive prepended. Remove only that first physical line,
# preserving every remaining byte and original line ending.
$auditSourceRoot = Join-Path $auditSketchPath 'src'
$sourceFiles = Get-ChildItem -LiteralPath $auditSourceRoot -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.c', '.h', '.hpp', '.json') }
foreach ($sourceFile in $sourceFiles) {
    $relative = $sourceFile.FullName.Substring($auditSourceRoot.Length).
        TrimStart('\')
    $destinationFile = Join-Path (Join-Path $DestinationPath 'src') $relative
    $destinationDirectory = Split-Path -Parent $destinationFile
    New-Item -ItemType Directory -Path $destinationDirectory -Force |
        Out-Null

    [byte[]]$bytes = [IO.File]::ReadAllBytes($sourceFile.FullName)
    $firstLf = [Array]::IndexOf($bytes, [byte]10)
    if ($firstLf -lt 0) {
        throw "Generated #line prefix was not found: $($sourceFile.FullName)"
    }
    [byte[]]$originalBytes = $bytes[($firstLf + 1)..($bytes.Length - 1)]
    [IO.File]::WriteAllBytes($destinationFile, $originalBytes)
}

# Recover TSP.ino from Arduino's merged sketch. The first two generated lines
# are not part of the original sketch; all following bytes are preserved.
$mergedSketchPath = Join-Path $auditSketchPath 'TSP.ino.cpp.merged'
[byte[]]$mergedBytes = [IO.File]::ReadAllBytes($mergedSketchPath)
$lineEnd = -1
for ($line = 0; $line -lt 2; ++$line) {
    $lineEnd = [Array]::IndexOf($mergedBytes, [byte]10, $lineEnd + 1)
    if ($lineEnd -lt 0) {
        throw 'Merged sketch does not contain the expected generated prefix'
    }
}
[byte[]]$inoBytes = $mergedBytes[($lineEnd + 1)..($mergedBytes.Length - 1)]
[IO.File]::WriteAllBytes((Join-Path $DestinationPath 'TSP.ino'), $inoBytes)

$rootFiles = @(
    'test.json',
    'CHANGELOG_2.0.1.md',
    'CHANGELOG_2.0.2.md',
    'CHANGELOG_2.0.3.md',
    'CHANGELOG_2.0.4.md',
    'DEV_LOG_2026-07-30.md',
    'DEV_LOG_2026-07-31.md',
    'HANDOFF.md',
    'PROJECT_HANDOFF_CURRENT.md',
    'README.md',
    'SESSION_HANDOFF_2026-07-31.md',
    '功能更新.md'
)
foreach ($name in $rootFiles) {
    $sourceFile = Join-Path $auditSketchPath $name
    if (Test-Path -LiteralPath $sourceFile) {
        [byte[]]$bytes = [IO.File]::ReadAllBytes($sourceFile)
        $firstLf = [Array]::IndexOf($bytes, [byte]10)
        if ($firstLf -lt 0) {
            throw "Generated #line prefix was not found: $sourceFile"
        }
        [byte[]]$originalBytes = $bytes[($firstLf + 1)..($bytes.Length - 1)]
        [IO.File]::WriteAllBytes((Join-Path $DestinationPath $name),
            $originalBytes)
    }
}

Write-Host "Restored Firmware 2.0.4 source: $DestinationPath"
Write-Host "Provenance audit BIN SHA-256: $auditBinHash"
Write-Host 'No MCU flashing was performed.'
