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

function Resolve-PythonLauncher {
    if (Get-Command py -ErrorAction SilentlyContinue) {
        & py -3 --version *> $null
        if ($LASTEXITCODE -eq 0) {
            return @{
                Kind = "py"
            }
        }
    }

    $pythonCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCmd) {
        $pythonPath = $pythonCmd.Source
        # Ignore Microsoft Store alias shims which are not a real Python installation.
        if ($pythonPath -and $pythonPath -notlike "*\WindowsApps\python*.exe") {
            & $pythonPath --version *> $null
            if ($LASTEXITCODE -eq 0) {
                return @{
                    Kind = "python"
                    Path = $pythonPath
                }
            }
        }
    }

    throw "Python 3 was not found. Install Python from python.org (or via winget/choco), then rerun this installer."
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
    $pythonLauncher = Resolve-PythonLauncher
    $peripheryDir = Join-Path $InstallDir "periphery"
    $venvDir = Join-Path $peripheryDir ".venv"
    $venvPython = Join-Path $venvDir "Scripts\python.exe"

    if ($pythonLauncher.Kind -eq "py") {
        & py -3 -m venv "$venvDir"
    } else {
        & $pythonLauncher.Path -m venv "$venvDir"
    }

    if (-not (Test-Path $venvPython)) {
        throw "Virtual environment creation failed. Python executable not found at '$venvPython'."
    }

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
