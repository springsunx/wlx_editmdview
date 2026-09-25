[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$Version = '0.5.9'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$distRoot = Join-Path $repoRoot 'dist'
$buildRoot = Join-Path $repoRoot 'build'
$stagingRoot = Join-Path $buildRoot ("package-{0}-{1}" -f $Version,
    [guid]::NewGuid().ToString('N'))
$archive = Join-Path $distRoot ("EditMdView-v{0}.zip" -f $Version)
$binaries = @(
    @{
        Source = Join-Path $repoRoot ("build-x86\bin\{0}\EditMdView.wlx" -f $Configuration)
        Name = 'EditMdView.wlx'
    },
    @{
        Source = Join-Path $repoRoot ("build\bin\{0}\EditMdView.wlx64" -f $Configuration)
        Name = 'EditMdView.wlx64'
    }
)
$saveHelper = Join-Path $repoRoot ("build-x86\bin\{0}\EditMdViewSave.exe" -f $Configuration)

foreach ($binary in $binaries) {
    if (-not (Test-Path -LiteralPath $binary.Source -PathType Leaf)) {
        throw "Build output not found: $($binary.Source). Run scripts/build.ps1 first."
    }
}
if (-not (Test-Path -LiteralPath $saveHelper -PathType Leaf)) {
    throw "Save helper not found: $saveHelper. Run scripts/build.ps1 first."
}

New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
New-Item -ItemType Directory -Path $stagingRoot | Out-Null
try {
    foreach ($binary in $binaries) {
        Copy-Item -LiteralPath $binary.Source -Destination (Join-Path $stagingRoot $binary.Name)
    }
    Copy-Item -LiteralPath $saveHelper -Destination (Join-Path $stagingRoot 'EditMdViewSave.exe')

    # Register the 32-bit basename. Total Commander automatically selects the
    # same-name .wlx64 sibling when running as a 64-bit process.
    $pluginInfo = Get-Content -LiteralPath (Join-Path $repoRoot 'pluginst.inf') -Raw
    $pluginInfo = [regex]::Replace($pluginInfo, '(?m)^file=.*$', 'file=EditMdView.wlx')
    $pluginInfoPath = Join-Path $stagingRoot 'pluginst.inf'
    [IO.File]::WriteAllText($pluginInfoPath, $pluginInfo, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination $stagingRoot
    Copy-Item -LiteralPath (Join-Path $repoRoot 'CHANGELOG.md') -Destination $stagingRoot
    Copy-Item -LiteralPath (Join-Path $repoRoot 'SHORTCUTS.txt') -Destination $stagingRoot
    Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\SCITE_PROPERTIES.md') -Destination $stagingRoot
    Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') -Destination $stagingRoot
    Get-ChildItem -LiteralPath (Join-Path $repoRoot 'config') -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $stagingRoot
    }

    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
    Compress-Archive -Path (Join-Path $stagingRoot '*') -DestinationPath $archive -CompressionLevel Optimal
} finally {
    $resolvedBuild = [IO.Path]::GetFullPath($buildRoot).TrimEnd('\') + '\'
    $resolvedStaging = [IO.Path]::GetFullPath($stagingRoot)
    if ($resolvedStaging.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedStaging)) {
        Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
    }
}

Write-Host "Packaged (x86 + x64): $archive"
