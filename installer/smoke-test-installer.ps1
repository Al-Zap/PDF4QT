# Installs the installer silently for the current user, checks files, shortcuts and registry entries,
# installs it again over itself (update), uninstalls it and checks that nothing is left. Meant for a CI
# runner, never for a computer in use: it installs and removes the application of the current user.
#   smoke-test-installer.ps1 -Installer build\install\PDF4QT-Setup-1.7.0.0.exe
param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [string]$AppName = 'PDF4QT Editor'
)
$ErrorActionPreference = 'Stop'

$progId = 'PDF4QT.Editor.pdf'
$appDir = Join-Path $env:LOCALAPPDATA "Programs\$AppName"
$startMenu = Join-Path ([Environment]::GetFolderPath('Programs')) "$AppName.lnk"
$desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) "$AppName.lnk"
$logDir = Join-Path (Split-Path -Parent (Resolve-Path $Installer).Path) 'installer-logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$script:failures = @()

function Check([bool]$condition, [string]$message) {
    if ($condition) { Write-Host "  ok    $message" }
    else { Write-Host "::error::$message"; $script:failures += $message }
}

function Get-RegValue([string]$path, [string]$name) {
    $item = Get-ItemProperty -Path $path -Name $name -ErrorAction SilentlyContinue
    if ($null -eq $item) { return $null }
    return $item.$name
}

function Get-PdfDefault {
    return Get-RegValue 'HKCU:\Software\Classes\.pdf' '(default)'
}

function Install([string]$log) {
    $process = Start-Process -FilePath $Installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/TASKS=desktopicon', "/LOG=`"$log`"") -Wait -PassThru
    Check ($process.ExitCode -eq 0) "installer exit code is 0 (was $($process.ExitCode))"
}

function Check-Installed {
    Check (Test-Path (Join-Path $appDir 'Pdf4QtEditor.exe')) "Pdf4QtEditor.exe installed in $appDir"
    Check (Test-Path (Join-Path $appDir 'unins000.exe')) 'uninstaller present'
    Check (Test-Path $startMenu) "Start menu shortcut $startMenu"
    Check (Test-Path $desktop) "desktop shortcut $desktop"
    $command = Get-RegValue "HKCU:\Software\Classes\$progId\shell\open\command" '(default)'
    Check ($command -like "*$appDir\Pdf4QtEditor.exe*%1*") "open command of $progId ($command)"
    Check ($null -ne (Get-RegValue 'HKCU:\Software\Classes\.pdf\OpenWithProgids' $progId)) '.pdf Open with entry'
    Check ((Get-RegValue 'HKCU:\Software\RegisteredApplications' $AppName) -eq 'Software\PDF4QT\Editor\Capabilities') 'registered application'
    Check ((Get-RegValue 'HKCU:\Software\PDF4QT\Editor\Capabilities\FileAssociations' '.pdf') -eq $progId) 'capabilities of .pdf'
    $uninstallKey = Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' | Where-Object { (Get-RegValue $_.PSPath 'DisplayName') -eq $AppName }
    Check ($null -ne $uninstallKey) 'listed in Apps & features (per user)'
}

$pdfDefaultBefore = Get-PdfDefault
Write-Host "Default of .pdf before: '$pdfDefaultBefore'"

Write-Host 'Installation'
Install (Join-Path $logDir 'install.log')
Check-Installed

Write-Host 'Update over the installed version'
Install (Join-Path $logDir 'update.log')
Check-Installed
$uninstallers = @(Get-ChildItem $appDir -Filter 'unins*.exe')
Check ($uninstallers.Count -eq 1) "single uninstaller after the update ($($uninstallers.Count))"

Check ((Get-PdfDefault) -eq $pdfDefaultBefore) 'default PDF application not changed by the installer'

Write-Host 'Uninstallation'
$process = Start-Process -FilePath (Join-Path $appDir 'unins000.exe') -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$(Join-Path $logDir 'uninstall.log')`"") -Wait -PassThru
Check ($process.ExitCode -eq 0) "uninstaller exit code is 0 (was $($process.ExitCode))"
# The uninstaller runs a copy of itself from the temporary directory and the started process ends at once
$deadline = (Get-Date).AddSeconds(180)
while ((Test-Path $appDir) -and ((Get-Date) -lt $deadline)) { Start-Sleep -Seconds 2 }

Check (-not (Test-Path $appDir)) "installation directory removed"
Check (-not (Test-Path $startMenu)) 'Start menu shortcut removed'
Check (-not (Test-Path $desktop)) 'desktop shortcut removed'
Check (-not (Test-Path "HKCU:\Software\Classes\$progId")) "$progId removed"
Check ($null -eq (Get-RegValue 'HKCU:\Software\Classes\.pdf\OpenWithProgids' $progId)) '.pdf Open with entry removed'
Check ($null -eq (Get-RegValue 'HKCU:\Software\RegisteredApplications' $AppName)) 'registered application removed'
Check (-not (Test-Path 'HKCU:\Software\PDF4QT')) 'capabilities removed'
Check (-not (Test-Path "HKCU:\Software\Classes\Applications\Pdf4QtEditor.exe")) 'application entry removed'
Check ((Get-PdfDefault) -eq $pdfDefaultBefore) 'default PDF application not changed by the uninstaller'

if ($script:failures.Count -gt 0) {
    Get-ChildItem $logDir -Filter '*.log' | ForEach-Object { Write-Host "----- $($_.Name)"; Get-Content $_.FullName -Tail 60 }
    throw "$($script:failures.Count) check(s) of the installer failed"
}
Write-Host 'Installer smoke test passed'
