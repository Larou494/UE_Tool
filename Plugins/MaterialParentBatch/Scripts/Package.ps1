[CmdletBinding()]
param([string]$ArtifactsRoot)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not $ArtifactsRoot) { $ArtifactsRoot = Join-Path $repositoryRoot 'Artifacts' }
$ArtifactsRoot = (Resolve-Path -LiteralPath $ArtifactsRoot).Path
$descriptor = Get-Content -LiteralPath (Join-Path $repositoryRoot 'MaterialParentBatch.uplugin') -Raw | ConvertFrom-Json
$releaseRoot = Join-Path $ArtifactsRoot ('Releases/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
[IO.Directory]::CreateDirectory($releaseRoot) | Out-Null
$packageRecords = @()
foreach ($build in Get-ChildItem -LiteralPath $ArtifactsRoot -Directory -Filter 'UE_*') {
    $result = Get-Content -LiteralPath (Join-Path $build.FullName 'Result.json') -Raw | ConvertFrom-Json
    if ($result.Compilation -ne 'Success' -or -not $result.TestsRun -or $result.Failed -ne 0 -or ($result.Succeeded + $result.SucceededWithWarnings) -ne 6) {
        throw "Refusing to package an unverified build: $($build.Name)"
    }
    $builtPlugin = Join-Path $build.FullName 'HostProject/Plugins/MaterialParentBatch'
    foreach ($sourceFile in Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'Source') -Recurse -File) {
        $relative = $sourceFile.FullName.Substring($repositoryRoot.Length + 1)
        if ((Get-FileHash -LiteralPath $sourceFile.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $builtPlugin $relative)).Hash) {
            throw "Build source differs from release source: $($build.Name)/$relative"
        }
    }
    $engineParts = $result.EngineVersion.Split('.')
    $engineMinor = "$($engineParts[0]).$($engineParts[1])"
    $packageName = "MaterialParentBatch_v$($descriptor.VersionName)_UE${engineMinor}_Win64"
    $stageParent = Join-Path $releaseRoot $packageName
    $stagePlugin = Join-Path $stageParent 'MaterialParentBatch'
    [IO.Directory]::CreateDirectory($stagePlugin) | Out-Null
    foreach ($item in @('Source', 'Config', 'MaterialParentBatch.uplugin', 'README.md', 'VALIDATION.md', 'CHANGELOG.md')) {
        Copy-Item -LiteralPath (Join-Path $repositoryRoot $item) -Destination $stagePlugin -Recurse
    }
    $binaryFolder = Join-Path $stagePlugin 'Binaries/Win64'
    [IO.Directory]::CreateDirectory($binaryFolder) | Out-Null
    foreach ($binary in @('UnrealEditor-MaterialParentBatch.dll', 'UnrealEditor.modules')) {
        Copy-Item -LiteralPath (Join-Path "$builtPlugin/Binaries/Win64" $binary) -Destination $binaryFolder
    }
    $packageDescriptor = Get-Content -LiteralPath (Join-Path $stagePlugin 'MaterialParentBatch.uplugin') -Raw | ConvertFrom-Json
    $packageDescriptor | Add-Member -NotePropertyName EngineVersion -NotePropertyValue $result.EngineVersion -Force
    $packageDescriptor | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $stagePlugin 'MaterialParentBatch.uplugin') -Encoding utf8
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $stagePlugin 'BUILD_INFO.json') -Encoding utf8
    $manifest = @(Get-ChildItem -LiteralPath $stagePlugin -File -Recurse | Sort-Object FullName | ForEach-Object {
        [PSCustomObject]@{Path=($_.FullName.Substring($stagePlugin.Length + 1) -replace '\\', '/');Bytes=$_.Length;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}
    })
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stagePlugin 'SHA256SUMS.json') -Encoding utf8
    $zipPath = Join-Path $releaseRoot "$packageName.zip"
    Compress-Archive -LiteralPath $stagePlugin -DestinationPath $zipPath -CompressionLevel Optimal
    $packageRecords += [PSCustomObject]@{File="$packageName.zip";EngineVersion=$result.EngineVersion;Bytes=(Get-Item -LiteralPath $zipPath).Length;SHA256=(Get-FileHash -LiteralPath $zipPath).Hash}
}
if ($packageRecords.Count -eq 0) { throw 'No verified builds found' }
$sourceParent = Join-Path $releaseRoot 'Source'
$sourceStage = Join-Path $sourceParent 'MaterialParentBatch'
[IO.Directory]::CreateDirectory($sourceStage) | Out-Null
foreach ($item in @('Source', 'Config', 'Scripts', 'MaterialParentBatch.uplugin', 'README.md', 'VALIDATION.md', 'CHANGELOG.md', '.gitignore', '.gitattributes')) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot $item) -Destination $sourceStage -Recurse
}
$sourceZip = Join-Path $releaseRoot "MaterialParentBatch_v$($descriptor.VersionName)_Source.zip"
Compress-Archive -LiteralPath $sourceStage -DestinationPath $sourceZip -CompressionLevel Optimal
$packageRecords += [PSCustomObject]@{File=[IO.Path]::GetFileName($sourceZip);EngineVersion='Source';Bytes=(Get-Item -LiteralPath $sourceZip).Length;SHA256=(Get-FileHash -LiteralPath $sourceZip).Hash}
$packageRecords | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.json') -Encoding utf8
$packageRecords | Format-Table -AutoSize
Write-Output "Release files: $releaseRoot"
