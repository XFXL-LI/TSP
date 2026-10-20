[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$CxxPath,
    [string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $OutputPath) {
    $OutputPath = Join-Path $repoRoot 'firmware_workspace/build/verification-2.1.2'
}
New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
$stubPath = Join-Path $PSScriptRoot 'reliability_stubs'
[string[]]$compilerPrefix = @()
if ([IO.Path]::GetFileNameWithoutExtension($CxxPath) -eq 'zig') {
    $compilerPrefix = @('c++')
}
foreach ($variant in @('standard', 'certified')) {
    $sourceRoot = Join-Path $repoRoot "firmware/$variant/TSP/src/module"
    $binary = Join-Path $OutputPath "reliability-$variant.exe"
    $compileArguments = $compilerPrefix + @(
        '-std=c++17', '-O0', '-g',
        '-I', $stubPath,
        '-I', (Join-Path $sourceRoot 'gas'),
        '-I', (Join-Path $sourceRoot 'file'),
        '-include', (Join-Path $stubPath 'reliability_mocks.h'),
        (Join-Path $PSScriptRoot 'firmware_reliability_test.cpp'),
        (Join-Path $sourceRoot 'gas/GasModbusAccess.cpp'),
        (Join-Path $sourceRoot 'file/SdMountSession.cpp'),
        '-o', $binary
    )
    & $CxxPath @compileArguments
    if ($LASTEXITCODE -ne 0) { throw "$variant mock-test compilation failed" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "$variant mock tests failed" }
    Write-Host "${variant}: software fault-injection tests passed (not hardware verification)."

    $statusBinary = Join-Path $OutputPath "get-data-$variant.exe"
    $statusArguments = $compilerPrefix + @(
        '-std=c++17',
        '-I', (Join-Path $repoRoot "firmware/$variant/TSP/src"),
        '-include', (Join-Path $stubPath 'posix_time_compat.h'),
        (Join-Path $PSScriptRoot 'get_data_status_test.cpp'),
        '-o', $statusBinary
    )
    & $CxxPath @statusArguments
    if ($LASTEXITCODE -ne 0) { throw "$variant get_data-test compilation failed" }
    & $statusBinary
    if ($LASTEXITCODE -ne 0) { throw "$variant get_data tests failed" }
    Write-Host "${variant}: get_data compatibility and reply-bound tests passed."
}
