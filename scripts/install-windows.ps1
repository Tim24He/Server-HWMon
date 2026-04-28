param(
    [string]$RepoUrl = "https://github.com/REPLACE_ME/Server_Stat_UI.git",
    [string]$Branch = "main",
    [string]$InstallDir = "$env:ProgramData\ServerStatUI",
    [string]$TaskName = "ServerStatPeripheryAgent"
)

$ErrorActionPreference = "Stop"

if ($RepoUrl -like "*REPLACE_ME*") {
    throw "RepoUrl is not set. Pass -RepoUrl with your GitHub repository URL."
}

function Assert-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Get-PythonCommand {
    if (Get-Command py -ErrorAction SilentlyContinue) {
        return "py -3"
    }
    if (Get-Command python -ErrorAction SilentlyContinue) {
        return "python"
    }
    throw "Python was not found. Install Python 3 first (including pip)."
}

function Clone-OrUpdateRepo {
    if (Test-Path "$InstallDir\.git") {
        git -C $InstallDir fetch --all --prune
        git -C $InstallDir checkout $Branch
        git -C $InstallDir pull --ff-only origin $Branch
    } else {
        $parent = Split-Path -Parent $InstallDir
        if (-not (Test-Path $parent)) {
            New-Item -ItemType Directory -Path $parent | Out-Null
        }
        git clone --branch $Branch $RepoUrl $InstallDir
    }
}

function Setup-PythonEnv {
    $pythonCmd = Get-PythonCommand
    $peripheryDir = Join-Path $InstallDir "periphery"
    $venvDir = Join-Path $peripheryDir ".venv"
    $venvPython = Join-Path $venvDir "Scripts\python.exe"

    & $env:ComSpec /c "$pythonCmd -m venv `"$venvDir`""
    & $venvPython -m pip install --upgrade pip wheel
    & $venvPython -m pip install psutil pyserial serial
}

function Install-StartupTask {
    $peripheryDir = Join-Path $InstallDir "periphery"
    $venvPython = Join-Path $peripheryDir ".venv\Scripts\python.exe"
    $mainPy = Join-Path $peripheryDir "main.py"

    $action = New-ScheduledTaskAction -Execute $venvPython -Argument "`"$mainPy`""
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -LogonType ServiceAccount -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable

    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
    Start-ScheduledTask -TaskName $TaskName
}

Assert-Command git

Clone-OrUpdateRepo
Setup-PythonEnv
Install-StartupTask

Write-Host "Install complete."
Write-Host "Task: $TaskName"
Write-Host "Check: Get-ScheduledTask -TaskName `"$TaskName`""
