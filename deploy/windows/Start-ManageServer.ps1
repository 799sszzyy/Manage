param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'manage.settings.psd1')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Manage.Common.ps1')

$settings = Get-ManageSettings -Path $ConfigPath
$serverPath = Join-Path $PSScriptRoot 'bin\manage-server.exe'
if (-not (Test-Path -LiteralPath $serverPath -PathType Leaf)) {
    throw "Application not found: $serverPath"
}

Assert-ManageHost ([string]$settings.Database.Host)
Assert-ManageIdentifier ([string]$settings.Database.Name) 'database name'
Assert-ManageIdentifier ([string]$settings.Database.User) 'database user'

$oldValues = @{
    MANAGE_DB_HOST = $env:MANAGE_DB_HOST
    MANAGE_DB_PORT = $env:MANAGE_DB_PORT
    MANAGE_DB_NAME = $env:MANAGE_DB_NAME
    MANAGE_DB_USER = $env:MANAGE_DB_USER
    MANAGE_DB_PASSWORD = $env:MANAGE_DB_PASSWORD
}
try {
    $env:MANAGE_DB_HOST = [string]$settings.Database.Host
    $env:MANAGE_DB_PORT = [string]$settings.Database.Port
    $env:MANAGE_DB_NAME = [string]$settings.Database.Name
    $env:MANAGE_DB_USER = [string]$settings.Database.User
    $env:MANAGE_DB_PASSWORD = Get-ManagePlainPassword

    $arguments = @(
        '--listen-address', [string]$settings.Server.ListenAddress,
        '--port', [string]$settings.Server.Port
    )
    if ([bool]$settings.Server.AllowLan) {
        $arguments += '--allow-lan'
    }

    Write-Host 'The server is running. Keep this window open; press Ctrl+C to stop it.'
    & $serverPath @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "manage-server exited with code $LASTEXITCODE"
    }
}
finally {
    foreach ($name in $oldValues.Keys) {
        [Environment]::SetEnvironmentVariable($name, $oldValues[$name], 'Process')
    }
}
