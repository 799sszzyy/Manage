param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'manage.settings.psd1')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Manage.Common.ps1')

$settings = Get-ManageSettings -Path $ConfigPath
$desktopPath = Join-Path $PSScriptRoot 'bin\manage-desktop.exe'
if (-not (Test-Path -LiteralPath $desktopPath -PathType Leaf)) {
    throw "Application not found: $desktopPath"
}

& $desktopPath --api-url ([string]$settings.Desktop.ApiUrl)
