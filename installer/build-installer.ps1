# Builds the per-user installer of the fork by Inno Setup 6 (preinstalled on the windows-2022 runner image,
# installed by Chocolatey when missing).
#   build-installer.ps1 -Version 1.7.0.0 -SourceDir build\install\usr\bin -OutputDir build\install
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$OutputDir
)
$ErrorActionPreference = 'Stop'

function Find-Iscc {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($root in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, "$env:LOCALAPPDATA\Programs")) {
        if (-not $root) { continue }
        $candidate = Join-Path $root 'Inno Setup 6\ISCC.exe'
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

$iscc = Find-Iscc
if (-not $iscc) {
    Write-Host 'Inno Setup not found, installing it by Chocolatey'
    choco install innosetup -y --no-progress
    if ($LASTEXITCODE -ne 0) { throw 'Installation of Inno Setup failed' }
    $iscc = Find-Iscc
    if (-not $iscc) { throw 'ISCC.exe not found after the installation of Inno Setup' }
}
Write-Host "Inno Setup compiler: $iscc"

$source = (Resolve-Path $SourceDir).Path
if (-not (Test-Path (Join-Path $source 'Pdf4QtEditor.exe'))) { throw "Pdf4QtEditor.exe not found in $source" }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$output = (Resolve-Path $OutputDir).Path
$script = Join-Path $PSScriptRoot 'pdf4qt.iss'

& $iscc "/DAppVersion=$Version" "/DSourceDir=$source" "/O$output" $script
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

$installer = Join-Path $output "PDF4QT-Setup-$Version.exe"
if (-not (Test-Path $installer)) { throw "Installer $installer was not created" }
Write-Host "Installer: $installer ($([math]::Round((Get-Item $installer).Length / 1MB, 1)) MB)"
