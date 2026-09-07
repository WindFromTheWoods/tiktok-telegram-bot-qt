[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $QtIfwRoot,

    [string] $PackageDirectory = ".packaging/packages",
    [string] $OutputDirectory = "artifacts/update-repository"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$repogen = Join-Path $QtIfwRoot "bin/repogen.exe"
if (-not (Test-Path -LiteralPath $repogen -PathType Leaf)) {
    throw "repogen.exe was not found under '$QtIfwRoot'."
}

$packages = (Resolve-Path -LiteralPath (
    Join-Path $projectRoot $PackageDirectory)).Path
$repository = Join-Path $projectRoot $OutputDirectory
if (Test-Path -LiteralPath $repository) {
    Remove-Item -LiteralPath $repository -Recurse -Force
}
New-Item -ItemType Directory -Path $repository -Force | Out-Null

& $repogen -p $packages $repository
if ($LASTEXITCODE -ne 0) {
    throw "Repository creation failed with exit code $LASTEXITCODE."
}

Write-Host "Update repository created: $repository"
