param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'manage.settings.psd1')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Manage.Common.ps1')

$settings = Get-ManageSettings -Path $ConfigPath
Assert-ManageHost ([string]$settings.Database.Host)
Assert-ManageIdentifier ([string]$settings.Database.Name) 'database name'
Assert-ManageIdentifier ([string]$settings.Database.User) 'database user'

$backupDirectory = [string]$settings.Backup.Directory
if (-not [IO.Path]::IsPathRooted($backupDirectory)) {
    $backupDirectory = Join-Path $PSScriptRoot $backupDirectory
}
New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupPath = Join-Path $backupDirectory "$($settings.Database.Name)-$timestamp.sql"
$dump = Get-ManageMySqlTool -Settings $settings -Name 'mysqldump.exe'
$oldPassword = $env:MYSQL_PWD
try {
    $env:MYSQL_PWD = Get-ManagePlainPassword
    & $dump `
        "--host=$($settings.Database.Host)" `
        "--port=$($settings.Database.Port)" `
        "--user=$($settings.Database.User)" `
        '--default-character-set=utf8mb4' `
        '--single-transaction' `
        '--routines' `
        '--triggers' `
        '--set-gtid-purged=OFF' `
        "--result-file=$backupPath" `
        ([string]$settings.Database.Name)
    if ($LASTEXITCODE -ne 0) {
        if (Test-Path -LiteralPath $backupPath) {
            Move-Item -LiteralPath $backupPath -Destination "$backupPath.failed"
        }
        throw "Database backup failed; mysqldump exit code: $LASTEXITCODE"
    }
}
finally {
    $env:MYSQL_PWD = $oldPassword
}

Write-Host "Backup completed: $backupPath"
$backupPath
