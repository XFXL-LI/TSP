[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$CxxPath,
    [Parameter(Mandatory=$true)][string]$CjsonRoot,
    [string]$OutputPath
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if(-not $OutputPath){$OutputPath=Join-Path $repoRoot 'firmware_workspace/build/verification-2.2.0-tvoc'}
New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
$nl=[Environment]::NewLine
$prefix=@()
if([IO.Path]::GetFileNameWithoutExtension($CxxPath) -eq 'zig'){$prefix=@('c++')}
function Strip-Includes([string]$s){[regex]::Replace($s,'(?m)^#include[^\r\n]*','')}
function Body([string]$s,[string]$pattern){
    $m=[regex]::Match($s,$pattern)
    if(-not $m.Success){throw "Cannot extract production body: $pattern"}
    $m.Groups[1].Value
}
# Reuse the established Arduino String double.
$upload=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'firmware_upload_test.cpp'))
$string=$upload.Substring(0,$upload.IndexOf('static std::vector<std::string> logs;'))
$cobj=Join-Path $OutputPath 'cJSON.o'
$cprefix=@();if($prefix.Count){$cprefix=@('cc')}
& $CxxPath @cprefix -std=c99 -c (Join-Path $CjsonRoot 'cJSON.c') -o $cobj
if($LASTEXITCODE -ne 0){throw 'cJSON compilation failed'}
foreach($variant in @('standard','certified')){
    $src=Join-Path $repoRoot "firmware/$variant/TSP/src"
    $generated=Join-Path $OutputPath $variant
    New-Item -ItemType Directory -Path $generated -Force | Out-Null
    $types=[IO.File]::ReadAllText((Join-Path $src 'inc/sys_init.h'))
    $config=[IO.File]::ReadAllText((Join-Path $src 'app/configManager/config.cpp'))
    $manager=[IO.File]::ReadAllText((Join-Path $src 'app/collectorManager/collectorManager.cpp'))
    $data=[IO.File]::ReadAllText((Join-Path $src 'app/dataManager/dataManager.cpp'))
    $sections=@{
        'test_string.h'=$string
        'production_types.h'=$types.Substring($types.IndexOf('enum class DataStatus'),$types.IndexOf('struct AllProcessedDataPacket')-$types.IndexOf('enum class DataStatus'))
        'adapter_source.h'=(Strip-Includes ([IO.File]::ReadAllText((Join-Path $src 'app/collectorManager/collect/Tvoc/TvocCollect.h'))))+$nl+(Strip-Includes ([IO.File]::ReadAllText((Join-Path $src 'app/collectorManager/collect/Tvoc/TvocCollect.cpp'))))
        'config_source.h'='namespace {'+$nl+$config.Substring($config.IndexOf('struct SensorDefinition'),$config.IndexOf('ConfigManager::ConfigManager()')-$config.IndexOf('struct SensorDefinition'))
        'stats_source.h'=(Body ([IO.File]::ReadAllText((Join-Path $src 'app/dataManager/dataManager.h'))) '(?ms)^(struct StatValue.*?^};)')
        'snapshot_source.h'=(Body $data '(?ms)^(bool DataManager::readLatestValues\(.*?^})')+$nl+(Body $data '(?ms)^(void DataManager::updateLatestSnapshot\(.*?^})')
        'collection_source.h'=(Body $manager '(?ms)^(bool deadlineReached\(.*?^})')+$nl+(Body $manager '(?ms)^(void waitUntil\(.*?^})')+$nl+(Body $manager '(?ms)^(void storePacket\(.*?^})')+$nl+$manager.Substring($manager.IndexOf('bool collectorManager::isTvocMode()'))
    }
    foreach($entry in $sections.GetEnumerator()){[IO.File]::WriteAllText((Join-Path $generated $entry.Key),$entry.Value,$utf8)}
    $binary=Join-Path $generated 'tvoc-tests.exe'
    & $CxxPath @prefix -std=c++17 -O0 -g -I $src -I $generated -I $CjsonRoot -I (Join-Path $PSScriptRoot 'upload_stubs') (Join-Path $PSScriptRoot 'tvoc_test.cpp') $cobj -o $binary
    if($LASTEXITCODE -ne 0){throw "$variant TVOC test compilation failed"}
    & $binary
    if($LASTEXITCODE -ne 0){throw "$variant TVOC regressions failed"}
    Write-Host "${variant}: TVOC regressions passed (not hardware verification)."
}
