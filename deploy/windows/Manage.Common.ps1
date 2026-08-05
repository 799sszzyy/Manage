Set-StrictMode -Version Latest

function Get-ManageSettings {
    param([string]$Path = (Join-Path $PSScriptRoot 'manage.settings.psd1'))

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Configuration not found: $Path. Copy manage.settings.psd1.example to manage.settings.psd1 first."
    }
    Import-PowerShellDataFile -LiteralPath $Path
}

function Assert-ManageIdentifier {
    param([Parameter(Mandatory = $true)][string]$Value, [string]$Label = 'value')

    if ($Value -notmatch '^[A-Za-z0-9_]+$') {
        throw "$Label may contain only letters, digits, and underscores."
    }
}

function Assert-ManageHost {
    param([Parameter(Mandatory = $true)][string]$Value)

    if ($Value -notmatch '^[A-Za-z0-9.:-]+$') {
        throw 'The database host contains unsupported characters.'
    }
}

function Get-ManagePlainPassword {
    if ($env:MANAGE_DB_PASSWORD) {
        return $env:MANAGE_DB_PASSWORD
    }

    $secure = Read-Host 'Enter the manage_app database password' -AsSecureString
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    }
}

function Get-ManageMySqlTool {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Settings,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $path = Join-Path ([string]$Settings.MySql.BinDirectory) $Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "MySQL tool not found: $path"
    }
    $path
}
