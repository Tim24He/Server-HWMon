param(
    [string]$RepoUrl = "https://github.com/Tim24He/Server-HWMon.git",
    [string]$Branch = "main",
    [string]$InstallDir = "$env:ProgramData\ServerHWMon",
    [string]$TaskName = "ServerHWMonPeripheryAgent"
)

$ErrorActionPreference = "Stop"

function Assert-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Ensure-LocalConfig {
    $peripheryDir = Join-Path $InstallDir "periphery"
    $localConfig = Join-Path $peripheryDir "periphery_config.local.json"
    $exampleConfig = Join-Path $peripheryDir "periphery_config.local.example.json"
    $defaultConfig = Join-Path $peripheryDir "periphery_config.json"

    if (Test-Path $localConfig) {
        return
    }

    if (Test-Path $exampleConfig) {
        Copy-Item -LiteralPath $exampleConfig -Destination $localConfig
        return
    }

    if (Test-Path $defaultConfig) {
        Copy-Item -LiteralPath $defaultConfig -Destination $localConfig
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
    & $venvPython -m pip install --no-cache-dir --upgrade pip wheel
    & $venvPython -m pip install --no-cache-dir psutil pyserial serial
}

function Cleanup-InstallArtifacts {
    $pipCachePaths = @(
        (Join-Path $env:LocalAppData "pip\Cache"),
        (Join-Path $env:ProgramData "pip\Cache"),
        (Join-Path $env:USERPROFILE "AppData\Local\pip\Cache")
    )

    foreach ($cachePath in $pipCachePaths) {
        if (Test-Path $cachePath) {
            Remove-Item -LiteralPath $cachePath -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Install-StartupTask {
    $peripheryDir = Join-Path $InstallDir "periphery"
    $venvPython = Join-Path $peripheryDir ".venv\Scripts\python.exe"
    $mainPy = Join-Path $peripheryDir "periphery_agent.py"
    $taskCmd = "`"$venvPython`" `"$mainPy`" 1>NUL 2>&1"
    $action = New-ScheduledTaskAction -Execute "cmd.exe" -Argument "/c $taskCmd"
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -LogonType ServiceAccount -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable

    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
    Start-ScheduledTask -TaskName $TaskName
}

Assert-Command git

Clone-OrUpdateRepo
Ensure-LocalConfig
Setup-PythonEnv
Cleanup-InstallArtifacts
Install-StartupTask

Write-Host "Install complete."
Write-Host "Task: $TaskName"
Write-Host "Check: Get-ScheduledTask -TaskName `"$TaskName`""
