[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $QtIfwRoot,

    [Parameter(Mandatory = $true)]
    [string] $UpdateRepositoryUrl,

    [string] $BuildDirectory = "build/release",
    [string] $Configuration = "Release",
    [string] $OutputFile = "artifacts/TikTokTelegramBot-2.1.0-Setup.exe"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuildDirectory = (Resolve-Path -LiteralPath (
    Join-Path $projectRoot $BuildDirectory)).Path
$binaryCreator = Join-Path $QtIfwRoot "bin/binarycreator.exe"
if (-not (Test-Path -LiteralPath $binaryCreator -PathType Leaf)) {
    throw "binarycreator.exe was not found under '$QtIfwRoot'."
}

$stagingRoot = Join-Path $projectRoot ".packaging"
$stagedPackages = Join-Path $stagingRoot "packages"
$packageData = Join-Path $stagedPackages "com.tiktoktelegrambot.application/data"
$generatedConfig = Join-Path $stagingRoot "config.xml"
$outputPath = Join-Path $projectRoot $OutputFile

if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot "installer/packages") -Destination $stagedPackages -Recurse
New-Item -ItemType Directory -Path $packageData -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $outputPath) -Force | Out-Null

& cmake --install $resolvedBuildDirectory --config $Configuration --prefix $packageData
if ($LASTEXITCODE -ne 0) {
    throw "CMake installation failed with exit code $LASTEXITCODE."
}

$escapedRepositoryUrl = [System.Security.SecurityElement]::Escape(
    $UpdateRepositoryUrl.TrimEnd("/"))
$configTemplate = Get-Content -LiteralPath (
    Join-Path $projectRoot "installer/config/config.xml.in") -Raw
$configTemplate.Replace("@UPDATE_REPOSITORY_URL@", $escapedRepositoryUrl) |
    Set-Content -LiteralPath $generatedConfig -Encoding UTF8

& $binaryCreator -c $generatedConfig -p $stagedPackages $outputPath
if ($LASTEXITCODE -ne 0) {
    throw "Installer creation failed with exit code $LASTEXITCODE."
}

Write-Host "Installer created: $outputPath"
