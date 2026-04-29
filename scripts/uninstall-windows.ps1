param(
    [string]$InstallDir = "$env:ProgramData\ServerHWMon",
    [string]$TaskName = "ServerHWMonPeripheryAgent",
    [bool]$RemoveInstallDir = $true
)

$ErrorActionPreference = "Stop"

function Remove-StartupTask {
    if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
        try {
            Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
        } catch {
        }
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    }
}

function Remove-InstalledFiles {
    if (-not $RemoveInstallDir) {
        return
    }
    if (Test-Path $InstallDir) {
        Remove-Item -LiteralPath $InstallDir -Recurse -Force
    }
}

Remove-StartupTask
Remove-InstalledFiles

Write-Host "Uninstall complete."
