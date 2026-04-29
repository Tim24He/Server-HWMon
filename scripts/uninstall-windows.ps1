param(
    [string]$InstallDir = "$env:ProgramData\ServerHWMon",
    [string]$TaskName = "ServerHWMonPeripheryAgent",
    [bool]$RemoveInstallDir = $true
)

$ErrorActionPreference = "Stop"
$LogPath = Join-Path $env:TEMP ("serverhwmon-uninstall-" + [guid]::NewGuid().ToString("N") + ".log")

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

$steps = @(
    @{ Label = "Removing startup task"; Action = { Remove-StartupTask } },
    @{ Label = "Removing installed files"; Action = { Remove-InstalledFiles } }
)

try {
    for ($i = 0; $i -lt $steps.Count; $i++) {
        $percent = [int](($i + 1) * 100 / $steps.Count)
        Write-Host ("[{0}%] {1}" -f $percent, $steps[$i].Label)
        Write-Progress -Activity "Server HWMon Uninstall" -Status $steps[$i].Label -PercentComplete $percent
        & $steps[$i].Action *>> $LogPath
    }
    Write-Progress -Activity "Server HWMon Uninstall" -Completed
    Remove-Item -LiteralPath $LogPath -Force -ErrorAction SilentlyContinue
    Write-Host "Uninstall successful."
} catch {
    Write-Progress -Activity "Server HWMon Uninstall" -Completed
    Write-Error "Uninstall failed. Log: $LogPath"
    if (Test-Path $LogPath) {
        Get-Content -Path $LogPath -Tail 120
    }
    throw
}
