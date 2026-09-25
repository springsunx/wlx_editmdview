[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64', 'x86', 'All')]
    [string]$Architecture = 'All'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'bootstrap-deps.ps1')

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Build Tools 2022 were not found.'
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'The Visual C++ x86/x64 build tools are not installed.'
}

$devShell = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$architectures = if ($Architecture -eq 'All') { @('x64', 'x86') } else { @($Architecture) }
foreach ($currentArchitecture in $architectures) {
    $is64Bit = $currentArchitecture -eq 'x64'
    $buildRoot = Join-Path $repoRoot $(if ($is64Bit) { 'build' } else { 'build-x86' })
    $generatorArchitecture = if ($is64Bit) { 'x64' } else { 'Win32' }
    $toolArchitecture = if ($is64Bit) { 'x64' } else { 'x86' }
    $pluginName = if ($is64Bit) { 'EditMdView.wlx64' } else { 'EditMdView.wlx' }

    # Some agent hosts expose both PATH and Path. MSBuild treats those as duplicate keys,
    # so remove the uppercase duplicate in the child shell before entering VsDevCmd.
    $command = 'set PATH=&& "{0}" -arch={1} -host_arch=x64 && "{2}" -S "{3}" -B "{4}" -G "Visual Studio 17 2022" -A {5} && "{2}" --build "{4}" --config {6} --parallel && "{7}" --test-dir "{4}" -C {6} --output-on-failure' -f $devShell, $toolArchitecture, $cmake, $repoRoot, $buildRoot, $generatorArchitecture, $Configuration, $ctest
    cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "$currentArchitecture build failed with exit code $LASTEXITCODE"
    }

    Write-Host "Built ($currentArchitecture): $(Join-Path $buildRoot ('bin\' + $Configuration + '\' + $pluginName))"
}
