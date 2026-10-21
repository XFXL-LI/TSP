[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$CxxPath,
    [string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $OutputPath) { $OutputPath = Join-Path $repoRoot 'firmware_workspace/build/verification-2.1.5' }
$utf8 = [Text.UTF8Encoding]::new($false)
$prefix = @()
if ([IO.Path]::GetFileNameWithoutExtension($CxxPath) -eq 'zig') { $prefix = @('c++') }
function Read-Source([string]$root, [string]$path) {
    return [IO.File]::ReadAllText((Join-Path $root $path))
}
function Without-Includes([string]$source) {
    return [regex]::Replace($source, '(?m)^#include[^\r\n]*', '')
}
foreach ($variant in @('standard', 'certified')) {
    $root = Join-Path $repoRoot "firmware/$variant/TSP/src"
    $generated = Join-Path $OutputPath "upload-$variant"
    New-Item -ItemType Directory -Path $generated -Force | Out-Null
    # Compile current production bodies and declarations, replacing only hardware,
    # clock and filesystem dependencies. No duplicate queue/CRC/format algorithms.
    $pack = Without-Includes (Read-Source $root 'module/pack212/pack212.cpp')
    $pack = [regex]::Replace($pack, '(?ms)^String HJ212_DataCenter::getCurrentQn\(\).*?^}', 'String HJ212_DataCenter::getCurrentQn() { return "20261020120000123"; }')
    $filesys = Read-Source $root 'app/filesysManager/filesysManager.cpp'
    $queue = [regex]::Match($filesys, '(?s)(bool filesysManager::queuePendingPacket\(.*?)(?=bool filesysManager::savePendingPacket\()')
    $protect = [regex]::Match($filesys, '(?ms)^(PendingProtectResult filesysManager::protectPendingPacket\(.*?^})')
    $sdLock = [regex]::Match($filesys, '(?ms)^(class ScopedSdLock.*?^};)')
    $store = [regex]::Match($filesys, '(?ms)^(void filesysManager::storeProcessedPacket\(.*?^})')
    $query = [regex]::Match($filesys, '(?ms)^(void filesysManager::processQuery\(.*?^})')
    $sdFile = Read-Source $root 'module/file/file_storage.cpp'
    $sdBodies = foreach ($name in @('SDcardInit', 'writeSDCard', 'readSDCard')) {
        $body = [regex]::Match($sdFile, "(?ms)^((?:bool|int) file_storage::$name\(.*?^})")
        if (-not $body.Success) { throw "Cannot extract $name" }
        $body.Groups[1].Value
    }
    if (-not $sdLock.Success -or -not $store.Success -or -not $query.Success) { throw 'Cannot extract SD admission/store/query implementation' }
    if (-not $queue.Success -or -not $protect.Success) { throw 'Cannot extract pending implementations' }
    $sections = @{
        'pack_source.h' = (Without-Includes (Read-Source $root 'module/pack212/pack212.h')) + "`n" + $pack
        'driver_source.h' = (Without-Includes (Read-Source $root 'module/dtu/dtu_driver.h')) + "`n" + (Without-Includes (Read-Source $root 'module/dtu/dtu_driver.cpp'))
        'guard_source.h' = Without-Includes (Read-Source $root 'module/dtu/Hj212TxGuard.h')
        'pending_decl.h' = Without-Includes (Read-Source $root 'app/filesysManager/filesysManager.h')
        'pending_source.h' = $queue.Groups[1].Value + "`n" + $protect.Groups[1].Value
        'diagnostic_source.h' = Without-Includes (Read-Source $root 'module/diagnostics/SdIoDiagnostics.cpp')
        'storage_source.h' = $sdLock.Groups[1].Value + "`n" + $store.Groups[1].Value
        'sd_file_source.h' = $sdBodies -join "`n"
        'query_source.h' = $query.Groups[1].Value
    }
    foreach ($entry in $sections.GetEnumerator()) { [IO.File]::WriteAllText((Join-Path $generated $entry.Key), $entry.Value, $utf8) }
    $binary = Join-Path $generated 'firmware-upload.exe'
    & $CxxPath @prefix -std=c++17 -O0 -g -I $root -I $generated -I (Join-Path $PSScriptRoot 'upload_stubs') (Join-Path $PSScriptRoot 'firmware_upload_test.cpp') -o $binary
    if ($LASTEXITCODE -ne 0) { throw "$variant upload-test compilation failed" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "$variant upload tests failed" }
    Write-Host "${variant}: current-source upload fault tests passed (not hardware verification)."
}
