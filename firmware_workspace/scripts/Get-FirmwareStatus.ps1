[CmdletBinding()]
param(
    [ValidateSet('standard', 'certified')]
    [string]$Variant = 'standard'
)

$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $workspaceRoot
$sourcePath = Join-Path $repoRoot ("firmware\{0}\TSP" -f $Variant)

function Get-VersionDefinition {
    param([string]$Path)
    $match = Select-String -LiteralPath $Path `
        -Pattern '^#define\s+VERSION2\s+"([^"]+)"' | Select-Object -First 1
    if (-not $match -or $match.Matches.Count -eq 0) {
        throw "VERSION2 definition was not found: $Path"
    }
    $match.Matches[0].Groups[1].Value
}

function Get-SourceFingerprint {
    param([string]$Path)
    $files = @(
        Get-Item -LiteralPath (Join-Path $Path 'TSP.ino')
        Get-Item -LiteralPath (Join-Path $Path 'test.json')
        Get-ChildItem -LiteralPath (Join-Path $Path 'src') -Recurse -File |
            Where-Object { $_.Extension -in @('.cpp', '.c', '.h', '.hpp', '.json') }
    ) | Sort-Object FullName
    $hashLines = foreach ($file in $files) {
        $relative = $file.FullName.Substring($Path.Length).TrimStart('\')
        "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash, $relative
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $fingerprint = [BitConverter]::ToString($sha.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes(($hashLines -join "`n")))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
    }
    [pscustomobject]@{ Count = $files.Count; Fingerprint = $fingerprint }
}

function Find-LatestManifest {
    param([string]$Root, [string]$ManifestName)
    $matches = foreach ($file in Get-ChildItem -LiteralPath $Root -Filter $ManifestName -Recurse -File -ErrorAction SilentlyContinue) {
        try {
            $manifest = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($manifest.variant -eq $Variant) {
                [pscustomobject]@{ File = $file; Manifest = $manifest }
            }
        }
        catch {
            Write-Warning "Could not read manifest: $($file.FullName)"
        }
    }
    $matches | Sort-Object {
        $dateValue = if ($_.Manifest.compiledAt) { $_.Manifest.compiledAt } else { $_.Manifest.packagedAt }
        try { [DateTimeOffset]$dateValue } catch { [DateTimeOffset]::MinValue }
    } -Descending | Select-Object -First 1
}

Write-Host 'TSP ESP32-S3 Firmware Status'
Write-Host "Variant: $Variant"
Write-Host "Current source: $sourcePath"
if (-not (Test-Path -LiteralPath (Join-Path $sourcePath 'TSP.ino'))) {
    throw "Current source does not exist: $sourcePath"
}
$inoVersion = Get-VersionDefinition (Join-Path $sourcePath 'TSP.ino')
$headerVersion = Get-VersionDefinition (Join-Path $sourcePath 'src\inc\sys_init.h')
$systemJsonText = Get-Content -LiteralPath `
    (Join-Path $sourcePath 'src\app\configManager\system_json.h') -Raw
$jsonMatch = [regex]::Match($systemJsonText, '"version"\s*:\s*"([^"]+)"')
if (-not $jsonMatch.Success) { throw 'JSON version was not found.' }
$jsonVersion = $jsonMatch.Groups[1].Value
$sourceState = Get-SourceFingerprint $sourcePath
Write-Host "VERSION2 (TSP.ino): $inoVersion"
Write-Host "VERSION2 (sys_init.h): $headerVersion"
Write-Host "JSON version: $jsonVersion"
Write-Host "Versions consistent: $($inoVersion -eq $headerVersion -and $inoVersion -eq $jsonVersion)"
Write-Host "Build inputs: $($sourceState.Count)"
Write-Host "Source fingerprint: $($sourceState.Fingerprint)"
$debugLine = Select-String -LiteralPath (Join-Path $sourcePath 'src\system\system\system.cpp') -Pattern '^#define DEBUG' -ErrorAction SilentlyContinue | Select-Object -First 1
Write-Host "DEBUG enabled: $([bool]$debugLine)"

$latestBuild = Find-LatestManifest (Join-Path $workspaceRoot 'build') 'build-manifest.json'
if ($latestBuild) {
    Write-Host ''
    Write-Host "Latest build: $($latestBuild.File.Directory.FullName)"
    Write-Host "Build time: $($latestBuild.Manifest.compiledAt)"
    Write-Host "Build status: $($latestBuild.Manifest.status)"
    Write-Host "Application SHA-256: $($latestBuild.Manifest.applicationSha256)"
}
else {
    Write-Host ''
    Write-Host 'Latest build: none found for this variant.'
}

$latestRelease = Find-LatestManifest (Join-Path $workspaceRoot 'releases') 'manifest.json'
if ($latestRelease) {
    Write-Host ''
    Write-Host "Latest release: $($latestRelease.File.Directory.FullName)"
    Write-Host "Release time: $($latestRelease.Manifest.packagedAt)"
    Write-Host "Release status: $($latestRelease.Manifest.status)"
    Write-Host "Application SHA-256: $($latestRelease.Manifest.files.'firmware.bin'.sha256)"
}
else {
    Write-Host ''
    Write-Host 'Latest release: none found for this variant.'
}

Write-Host ''
Write-Host 'Constraints: hourly statistics unchanged; HJ212 gap 3000ms; SHT30 separate; DEBUG enabled.'
Write-Host 'Flash guard: -Port and exact -ConfirmFlash FLASH are mandatory.'
