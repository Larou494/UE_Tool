# Build and test only an isolated host project, never the user's game project.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [string]$OutputRoot,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$version = Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
if ($version.MajorVersion -ne 5 -or $version.MinorVersion -lt 5 -or $version.MinorVersion -gt 8) {
    throw 'This release targets Unreal Engine 5.5 through 5.8.'
}
$engineVersion = "$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)"
if (-not $OutputRoot) { $OutputRoot = Join-Path $repositoryRoot "Artifacts/UE_$engineVersion" }
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
[IO.Directory]::CreateDirectory($OutputRoot) | Out-Null
# Invalidate a previous successful result before any new build can fail.
[PSCustomObject]@{Compilation='InProgress';TestsRun=$false;EngineVersion=$engineVersion} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputRoot 'Result.json') -Encoding utf8
$hostRoot = Join-Path $OutputRoot 'HostProject'
$hostPlugin = Join-Path $hostRoot 'Plugins/MaterialParentBatch'
[IO.Directory]::CreateDirectory($hostPlugin) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $hostRoot 'Source')) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $hostRoot 'Content')) | Out-Null
foreach ($item in @('Source', 'Config', 'MaterialParentBatch.uplugin', 'README.md', 'VALIDATION.md')) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot $item) -Destination $hostPlugin -Recurse -Force
}
$projectFile = Join-Path $hostRoot 'MPBHost.uproject'
$projectJson = '{"FileVersion":3,"Plugins":[{"Name":"MaterialParentBatch","Enabled":true}]}'
[IO.File]::WriteAllText($projectFile, $projectJson)
$targetSource = @'
using UnrealBuildTool;
// Match the installed editor's build defaults to keep shared engine binaries compatible.
public class MPBHostEditorTarget : TargetRules
{
    public MPBHostEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        bBuildAllModules = false;
    }
}
'@
[IO.File]::WriteAllText((Join-Path $hostRoot 'Source/MPBHostEditor.Target.cs'), $targetSource)
$buildScript = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
$buildLog = Join-Path $OutputRoot 'Build.log'
$buildArguments = @('MPBHostEditor', 'Win64', 'Development', "-Project=$projectFile", '-NoHotReloadFromIDE', '-NoLiveCoding', '-WaitMutex', '-NoUBA', '-DisableUnity', '-MaxParallelActions=4')
& $buildScript @buildArguments 2>&1 | Tee-Object -FilePath $buildLog
if ($LASTEXITCODE -ne 0) { throw "UE $engineVersion compilation failed; see $buildLog" }
$testReport = $null
if (-not $SkipTests) {
    $editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $reportFolder = Join-Path $OutputRoot 'TestReport'
    $testLog = Join-Path $OutputRoot 'Automation.log'
    # UE 5.5 rejects long filesystem DDC roots; keep this process's cache path short.
    $testCache = Join-Path ([IO.Path]::GetTempPath()) "MPB-DDC/$engineVersion"
    [IO.Directory]::CreateDirectory($testCache) | Out-Null
    $hostConfig = Join-Path $hostRoot 'Config'
    [IO.Directory]::CreateDirectory($hostConfig) | Out-Null
    $cacheConfig = '[InstalledDerivedDataBackendGraph]' + "`n" + 'Local=(Type=FileSystem,DeleteOnly=true,UnusedFileAge=8,Path="' + ($testCache -replace '\\', '/') + '")' + "`n"
    [IO.File]::WriteAllText((Join-Path $hostConfig 'DefaultEngine.ini'), $cacheConfig)
    $testArguments = @($projectFile, '-unattended', '-nop4', '-nosplash', '-RenderOffscreen', '-d3d12', '-nosound', '-stdout', '-FullStdOutLogOutput', '-ExecCmds=Automation RunTests MaterialParentBatch', '-TestExit=Automation Test Queue Empty', "-ReportExportPath=$reportFolder", "-abslog=$testLog")
    & $editor @testArguments 2>&1 | Tee-Object -FilePath (Join-Path $OutputRoot 'Automation-stdout.log')
    if ($LASTEXITCODE -ne 0) { throw "UE $engineVersion automation process failed; see $testLog" }
    $testReport = Get-Content -LiteralPath (Join-Path $reportFolder 'index.json') -Raw | ConvertFrom-Json
    if ($testReport.failed -ne 0 -or $testReport.notRun -ne 0 -or $testReport.inProcess -ne 0 -or @($testReport.tests).Count -ne 6 -or @($testReport.tests | Where-Object state -ne 'Success').Count -ne 0) {
        throw "UE $engineVersion did not pass all six tests; see $reportFolder"
    }
}
$summary = [PSCustomObject]@{
    EngineVersion = $engineVersion
    Changelist = $version.Changelist
    CompatibleChangelist = $version.CompatibleChangelist
    Configuration = 'Win64 Development Editor'
    Compilation = 'Success'
    TestsRun = (-not $SkipTests)
    Succeeded = $(if ($testReport) { $testReport.succeeded } else { 0 })
    SucceededWithWarnings = $(if ($testReport) { $testReport.succeededWithWarnings } else { 0 })
    Failed = $(if ($testReport) { $testReport.failed } else { 0 })
    CompletedAt = (Get-Date).ToString('o')
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputRoot 'Result.json') -Encoding utf8
$summary | ConvertTo-Json
