[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$CxxPath,
    [Parameter(Mandatory = $true)] [string]$CjsonRoot,
    [string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $OutputPath) {
    $OutputPath = Join-Path $repoRoot 'firmware_workspace/build/verification-2.1.3'
}
New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
$isZig = [IO.Path]::GetFileNameWithoutExtension($CxxPath) -eq 'zig'
$cPrefix = @()
$cppPrefix = @()
if ($isZig) { $cPrefix = @('cc'); $cppPrefix = @('c++') }
$cjsonObject = Join-Path $OutputPath 'cJSON.o'
& $CxxPath @cPrefix -std=c99 -c (Join-Path $CjsonRoot 'cJSON.c') -o $cjsonObject
if ($LASTEXITCODE -ne 0) { throw 'cJSON compilation failed' }

function Extract-Required([string]$source, [string]$pattern, [string]$name) {
    $match = [regex]::Match($source, $pattern)
    if (-not $match.Success) { throw "Cannot extract current production implementation: $name" }
    return $match.Groups[1].Value
}
foreach ($variant in @('standard', 'certified')) {
    $sourceRoot = Join-Path $repoRoot "firmware/$variant/TSP/src"
    $generated = Join-Path $OutputPath $variant
    New-Item -ItemType Directory -Path $generated -Force | Out-Null
    # Extract current source bodies, not a second implementation of the algorithm.
    # Only Arduino/RTOS/sensor/serial dependencies are replaced by host mocks.
    $config = [IO.File]::ReadAllText((Join-Path $sourceRoot 'app/configManager/config.cpp'))
    $configBody = Extract-Required $config '(?s)(static SwitchConfigPolicy::BoolField booleanField.*?)(?=struct SensorDefinition)' 'switch helpers'
    $tempHeader = [IO.File]::ReadAllText((Join-Path $sourceRoot 'app/tempManager/tempManager.h'))
    $tempClass = Extract-Required $tempHeader '(?s)(class TempManager.*?\n};)' 'TempManager class'
    $tempCpp = [IO.File]::ReadAllText((Join-Path $sourceRoot 'app/tempManager/tempManager.cpp'))
    $tempBody = Extract-Required $tempCpp '(?s)(TempManager::TempManager\(\).*\z)' 'TempManager implementation'
    $ledHeader = [IO.File]::ReadAllText((Join-Path $sourceRoot 'app/ledManager/ledManager.h'))
    $ledClass = Extract-Required $ledHeader '(?s)(class LedManager.*?\n};)' 'LedManager class'
    $ledCpp = [IO.File]::ReadAllText((Join-Path $sourceRoot 'app/ledManager/ledManager.cpp'))
    $ledBody = Extract-Required $ledCpp '(?s)(LedManager::LedManager\(\).*\z)' 'LedManager implementation'
    $wire = [IO.File]::ReadAllText((Join-Path $sourceRoot 'module/QyledLib/QyledLib.c'))
    $checksum = Extract-Required $wire '(?ms)^(uint8_t check_sum\([^\n]*\n\{.*?^\})' 'LED checksum'
    $pack = Extract-Required $wire '(?ms)^(uint16_t pack485Buffer\([^\n]*\n\{.*?^\})' 'LED packet'
    $utf8 = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText((Join-Path $generated 'switch_source.h'), $configBody, $utf8)
    [IO.File]::WriteAllText((Join-Path $generated 'thermal_source.h'), "$tempClass`n$tempBody", $utf8)
    [IO.File]::WriteAllText((Join-Path $generated 'led_source.h'), "$checksum`n$pack`n$ledClass`n$ledBody", $utf8)
    $events = [IO.File]::ReadAllText((Join-Path $sourceRoot 'system/event/eventBus.cpp'))
    $publish = Extract-Required $events '(?ms)^(void EventBus::publish\([^\n]*\n?\{.*?^\})' 'EventBus publish'
    [IO.File]::WriteAllText((Join-Path $generated 'event_source.h'), $publish, $utf8)
    $binary = Join-Path $generated 'firmware-controls.exe'
    $arguments = $cppPrefix + @(
        '-std=c++17', '-O0', '-g', '-I', $sourceRoot,
        '-I', $generated, '-I', $CjsonRoot,
        (Join-Path $PSScriptRoot 'firmware_control_test.cpp'),
        $cjsonObject, '-o', $binary
    )
    & $CxxPath @arguments
    if ($LASTEXITCODE -ne 0) { throw "$variant control-test compilation failed" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "$variant control tests failed" }
    Write-Host "${variant}: current-source configuration, LED and heating mock tests passed (not hardware verification)."
}
