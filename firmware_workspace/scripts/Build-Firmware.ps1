[CmdletBinding()]
param(
    [ValidateSet('standard', 'certified')]
    [string]$Variant = 'standard',
    [string]$SourcePath,
    [string]$BuildPath,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $workspaceRoot

$variantSourcePaths = @{
    standard = Join-Path $repoRoot 'firmware\standard\TSP'
    certified = Join-Path $repoRoot 'firmware\certified\TSP'
}
if (-not $SourcePath) {
    $SourcePath = $variantSourcePaths[$Variant]
}
if (-not $BuildPath) {
    $BuildPath = Join-Path $workspaceRoot ("build\current-{0}" -f $Variant)
}

$SourcePath = [IO.Path]::GetFullPath($SourcePath)
$BuildPath = [IO.Path]::GetFullPath($BuildPath)
$allowedBuildRoot = [IO.Path]::GetFullPath((Join-Path $workspaceRoot 'build'))
$cli = Join-Path $workspaceRoot 'toolchain\arduino-cli.exe'
$fqbn = 'esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default'

if (-not (Test-Path -LiteralPath (Join-Path $SourcePath 'TSP.ino'))) {
    throw "Invalid source directory: $SourcePath"
}
if (-not (Test-Path -LiteralPath (Join-Path $SourcePath 'test.json'))) {
    throw "Source directory has no test.json: $SourcePath"
}
if (-not (Test-Path -LiteralPath $cli)) {
    throw "arduino-cli not found: $cli"
}
if (-not $BuildPath.StartsWith($allowedBuildRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Build directory must be under $allowedBuildRoot; refused: $BuildPath"
}

function Get-SourceState {
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
    $hashText = ($hashLines -join "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $fingerprint = [BitConverter]::ToString(
            $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($hashText))
        ).Replace('-', '')
    }
    finally {
        $sha.Dispose()
    }
    [pscustomobject]@{
        Files = $files
        HashLines = $hashLines
        Fingerprint = $fingerprint
    }
}

function Get-VersionDefinition {
    param([string]$Path)

    $match = Select-String -LiteralPath $Path `
        -Pattern '^#define\s+VERSION2\s+"([^"]+)"' | Select-Object -First 1
    if (-not $match -or $match.Matches.Count -eq 0) {
        throw "VERSION2 definition was not found: $Path"
    }
    $match.Matches[0].Groups[1].Value
}

$inoVersion = Get-VersionDefinition (Join-Path $SourcePath 'TSP.ino')
$headerVersion = Get-VersionDefinition (Join-Path $SourcePath 'src\inc\sys_init.h')
$systemJsonPath = Join-Path $SourcePath 'src\app\configManager\system_json.h'
$systemJsonText = Get-Content -LiteralPath $systemJsonPath -Raw
$systemJsonMatch = [regex]::Match($systemJsonText, '"version"\s*:\s*"([^"]+)"')
if (-not $systemJsonMatch.Success) {
    throw "JSON version was not found: $systemJsonPath"
}
$jsonVersion = $systemJsonMatch.Groups[1].Value
if ($inoVersion -ne $headerVersion -or $inoVersion -ne $jsonVersion) {
    throw "Version mismatch: TSP.ino=$inoVersion, sys_init.h=$headerVersion, system_json.h=$jsonVersion"
}
$sourceStateBefore = Get-SourceState $SourcePath
$sourceFingerprint = $sourceStateBefore.Fingerprint
$sourceHashLines = $sourceStateBefore.HashLines
$firmwareVersion = $inoVersion

Write-Host "Variant: $Variant"
Write-Host "Source: $SourcePath"
Write-Host "Output: $BuildPath"
Write-Host "Version: $firmwareVersion"
Write-Host "Build inputs: $($sourceStateBefore.Files.Count)"
Write-Host "Source fingerprint: $sourceFingerprint"
if ($DryRun) {
    Write-Host 'Dry check passed; no build directory was created and no compilation was performed.'
    return
}

if (Test-Path -LiteralPath $BuildPath) {
    Remove-Item -LiteralPath $BuildPath -Recurse -Force
}
New-Item -ItemType Directory -Path $BuildPath | Out-Null

Write-Host 'Compile only; no MCU flashing will be performed.'

& $cli compile --fqbn $fqbn --build-path $BuildPath $SourcePath
if ($LASTEXITCODE -ne 0) {
    throw "Compile failed; arduino-cli exit code: $LASTEXITCODE"
}

$coreBootApp = Join-Path $env:LOCALAPPDATA `
    'Arduino15\packages\esp32\hardware\esp32\3.3.7\tools\partitions\boot_app0.bin'
if (-not (Test-Path -LiteralPath $coreBootApp)) {
    throw "Compile passed but ESP32 Core 3.3.7 boot_app0.bin was not found: $coreBootApp"
}
Copy-Item -LiteralPath $coreBootApp -Destination (Join-Path $BuildPath 'boot_app0.bin')

$required = @(
    'TSP.ino.bin',
    'TSP.ino.bootloader.bin',
    'TSP.ino.partitions.bin',
    'boot_app0.bin',
    'partitions.csv'
)
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildPath $name))) {
        throw "Required build output is missing: $name"
    }
}

$expectedPartitions = @(
    'nvs,data,nvs,0x9000,0x5000',
    'otadata,data,ota,0xe000,0x2000',
    'app0,app,ota_0,0x10000,0x300000',
    'app1,app,ota_1,0x310000,0x300000',
    'ffat,data,fat,0x610000,0x9E0000',
    'coredump,data,coredump,0xFF0000,0x10000'
)
$actualPartitions = Get-Content -LiteralPath (Join-Path $BuildPath 'partitions.csv') |
    Where-Object { $_ -notmatch '^\s*#' -and $_ -match ',' } |
    ForEach-Object { (($_ -split ',')[0..4] | ForEach-Object { $_.Trim() }) -join ',' }
if (($actualPartitions -join "`n") -cne ($expectedPartitions -join "`n")) {
    throw 'Generated partition layout does not match app3M_fat9M_16MB.'
}

$sourceStateAfter = Get-SourceState $SourcePath
if ($sourceStateAfter.Fingerprint -ne $sourceFingerprint) {
    throw 'Source inputs changed during compilation; build is not accepted.'
}

$app = Join-Path $BuildPath 'TSP.ino.bin'
$appHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $app).Hash
$debugDefinition = Select-String -LiteralPath (Join-Path $SourcePath 'src\system\system\system.cpp') `
    -Pattern '^\s*#\s*define\s+DEBUG(?:\s|$)' | Select-Object -First 1
$debugConstraint = if ($debugDefinition) {
    'DEBUG enabled (LOG_LEVEL_DEBUG)'
} else {
    'DEBUG disabled (LOG_LEVEL_INFO)'
}

$manifest = [ordered]@{
    firmwareVersion = $firmwareVersion
    variant = $Variant
    status = 'compiled-not-hardware-verified'
    compiledAt = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ssK')
    sourcePath = $SourcePath
    buildInputCount = $sourceStateBefore.Files.Count
    sourceFingerprintSha256 = $sourceFingerprint
    esp32Core = '3.3.7'
    fqbn = $fqbn
    applicationBytes = (Get-Item -LiteralPath $app).Length
    applicationSha256 = $appHash
    constraints = @(
        'hourly statistics unchanged',
        'HJ212 packet gap 3000ms',
        'SHT30 handled independently',
        $debugConstraint
    )
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $BuildPath 'build-manifest.json') -Encoding UTF8
$sourceHashLines | Set-Content -LiteralPath (Join-Path $BuildPath 'source-files.sha256') -Encoding UTF8

Write-Host ''
Write-Host 'Compile passed.'
Write-Host "Application BIN: $app"
Write-Host "Bytes: $((Get-Item -LiteralPath $app).Length)"
Write-Host "SHA-256: $appHash"

$verifiedManifest = Join-Path $workspaceRoot 'releases\2.0.4\2026-08-10_hw-verified\manifest.json'
if (Test-Path -LiteralPath $verifiedManifest) {
    $verified = Get-Content -LiteralPath $verifiedManifest -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($verified.files.'firmware.bin'.sha256 -eq $appHash) {
        Write-Host 'Comparison: identical to the application BIN hardware-tested on 2026-08-10.'
    }
    elseif ($verified.sourceFingerprintSha256 -eq $sourceFingerprint) {
        Write-Warning 'Comparison: source inputs match the verified release, but the BIN differs (for example, compile-time metadata). This build is still not hardware-verified.'
    }
    else {
        Write-Warning 'Comparison: differs from the 2026-08-10 hardware-tested BIN; this build is not hardware-verified.'
    }
}
