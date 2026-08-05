param(
    [Parameter(Mandatory = $true)][string]$BackupFile,
    [Parameter(Mandatory = $true)][string]$ConfirmDatabaseName,
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'manage.settings.psd1')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Manage.Common.ps1')

$settings = Get-ManageSettings -Path $ConfigPath
$databaseName = [string]$settings.Database.Name
if ($ConfirmDatabaseName -cne $databaseName) {
    throw "Restore confirmation failed: ConfirmDatabaseName must exactly equal $databaseName."
}
Assert-ManageHost ([string]$settings.Database.Host)
Assert-ManageIdentifier $databaseName 'database name'
Assert-ManageIdentifier ([string]$settings.Database.User) 'database user'
$resolvedBackup = (Resolve-Path -LiteralPath $BackupFile).Path
$mysql = Get-ManageMySqlTool -Settings $settings -Name 'mysql.exe'

$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $mysql
$startInfo.Arguments = "--host=$($settings.Database.Host) --port=$($settings.Database.Port) --user=$($settings.Database.User) --database=$databaseName --default-character-set=utf8mb4"
$startInfo.UseShellExecute = $false
# Keep the restore process attached and visible. Operational scripts in this
# project do not hide child processes or weaken endpoint protection.
$startInfo.CreateNoWindow = $false
$startInfo.RedirectStandardInput = $true
$startInfo.RedirectStandardError = $true
$startInfo.StandardInputEncoding = [Text.UTF8Encoding]::new($false)

$oldPassword = $env:MYSQL_PWD
try {
    $env:MYSQL_PWD = Get-ManagePlainPassword
    $process = [Diagnostics.Process]::Start($startInfo)
    $reader = [IO.StreamReader]::new($resolvedBackup, [Text.UTF8Encoding]::new($false), $true)
    try {
        $reader.CopyTo($process.StandardInput)
        $process.StandardInput.Close()
    }
    finally {
        $reader.Dispose()
    }
    $errorText = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Database restore failed; mysql exit code $($process.ExitCode): $errorText"
    }
}
finally {
    $env:MYSQL_PWD = $oldPassword
}

Write-Host "Restore completed: $resolvedBackup -> $databaseName"
