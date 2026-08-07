param(
    [string]$BuildDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) 'build-final'),
    [string]$QtDirectory = 'F:\Qt\6.8.3\msvc2022_64',
    [string]$MySqlRoot = 'C:\Program Files\MySQL\MySQL Server 8.4',
    [string]$OutputRoot = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) 'dist')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $env:VCINSTALLDIR) {
    throw 'Run this script in Developer PowerShell for VS 2022 so MSVC runtimes can be deployed.'
}

$multiConfigRelease = Join-Path $BuildDirectory 'Release'
$releaseDirectory = if (Test-Path -LiteralPath (Join-Path $multiConfigRelease 'manage-desktop.exe')) {
    $multiConfigRelease
} else {
    # Ninja is a single-config generator, so its Release binaries live directly
    # in the build directory. Visual Studio generators use the Release subfolder.
    $BuildDirectory
}
$desktop = Join-Path $releaseDirectory 'manage-desktop.exe'
$server = Join-Path $releaseDirectory 'manage-server.exe'
$windeployqt = Join-Path $QtDirectory 'bin\windeployqt.exe'
$mysqlLibrary = Join-Path $MySqlRoot 'lib\libmysql.dll'
$mysqlRuntimeLibraries = @(
    (Join-Path $MySqlRoot 'bin\libssl-3-x64.dll'),
    (Join-Path $MySqlRoot 'bin\libcrypto-3-x64.dll')
)
$qmysql = Join-Path $QtDirectory 'plugins\sqldrivers\qsqlmysql.dll'
foreach ($path in @($desktop, $server, $windeployqt, $mysqlLibrary, $qmysql) + $mysqlRuntimeLibraries) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Packaging dependency not found: $path"
    }
}

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$packageDirectory = Join-Path $OutputRoot "Manage-v0.8.0-$timestamp"
$binDirectory = Join-Path $packageDirectory 'bin'
New-Item -ItemType Directory -Path $binDirectory -Force | Out-Null
Copy-Item -LiteralPath $desktop, $server -Destination $binDirectory

& $windeployqt --release --no-translations --compiler-runtime (Join-Path $binDirectory 'manage-desktop.exe')
if ($LASTEXITCODE -ne 0) { throw 'Desktop Qt runtime deployment failed.' }
& $windeployqt --release --no-translations --compiler-runtime (Join-Path $binDirectory 'manage-server.exe')
if ($LASTEXITCODE -ne 0) { throw 'Server Qt runtime deployment failed.' }

$sqlDriverDirectory = Join-Path $binDirectory 'sqldrivers'
New-Item -ItemType Directory -Path $sqlDriverDirectory -Force | Out-Null
Copy-Item -LiteralPath $qmysql -Destination $sqlDriverDirectory -Force
Copy-Item -LiteralPath $mysqlLibrary -Destination $binDirectory -Force
Copy-Item -LiteralPath $mysqlRuntimeLibraries -Destination $binDirectory -Force

Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' |
    Where-Object Name -ne 'Package-Manage.ps1' |
    Copy-Item -Destination $packageDirectory
Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.cmd' |
    Copy-Item -Destination $packageDirectory
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'manage.settings.psd1.example') -Destination $packageDirectory

$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$manuals = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'docs') -Filter '*.docx' -File
foreach ($manual in $manuals) {
    Copy-Item -LiteralPath $manual.FullName -Destination $packageDirectory
}

Write-Host "Package completed: $packageDirectory"
$packageDirectory
