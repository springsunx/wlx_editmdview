[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$downloadRoot = Join-Path $repoRoot '.deps\downloads'
$sourceRoot = Join-Path $repoRoot '.deps\src'

$packages = @(
    @{
        Name = 'scintilla566.zip'
        Uri = 'https://www.scintilla.org/scintilla566.zip'
        Sha256 = 'A0C0CDF1CF226DC6252020CE9A87A939A8B615E65269DB40AC9123B28FA20A9D'
        Destination = 'scintilla'
    },
    @{
        Name = 'lexilla553.zip'
        Uri = 'https://www.scintilla.org/lexilla553.zip'
        Sha256 = '2092B1DD18355321717E3BDE25148E4C87E691723CA2B06A65E29A307C5462A6'
        Destination = 'lexilla'
    },
    @{
        Name = 'md4c-0.5.3.zip'
        Uri = 'https://github.com/mity/md4c/archive/refs/tags/release-0.5.3.zip'
        Sha256 = 'C5B2966CAF3CE9F3B97FB7672DB460C95DABD387CA57873F93979E7A9AE93026'
        Destination = 'md4c'
    },
    @{
        Name = 'Microsoft.Web.WebView2.1.0.4191.47.zip'
        Uri = 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.4191.47'
        Sha256 = 'F492BBF547D0DA329553B6727435B677579B1E9F91CC9E4A1AD029366D5F23D0'
        Destination = 'webview2'
    }
)

New-Item -ItemType Directory -Force -Path $downloadRoot, $sourceRoot | Out-Null

foreach ($package in $packages) {
    $archivePath = Join-Path $downloadRoot $package.Name
    $destinationPath = Join-Path $sourceRoot $package.Destination

    if (-not (Test-Path -LiteralPath $archivePath)) {
        Write-Host "Downloading $($package.Name)..."
        Invoke-WebRequest -Uri $package.Uri -OutFile $archivePath
    }

    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($actualHash -ne $package.Sha256) {
        throw "Checksum mismatch for $($package.Name). Expected $($package.Sha256), got $actualHash"
    }

    if (-not (Test-Path -LiteralPath $destinationPath)) {
        Write-Host "Extracting $($package.Name)..."
        Expand-Archive -LiteralPath $archivePath -DestinationPath $destinationPath
    }
}
