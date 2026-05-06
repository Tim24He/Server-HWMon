param(
    [string]$RepoUrl = "https://github.com/Tim24He/Server-HWMon.git",
    [string]$Branch = "main",
    [string]$InstallDir = "$env:ProgramData\ServerHWMon",
    [string]$TaskName = "ServerHWMonPeripheryAgent"
)

$ErrorActionPreference = "Stop"
$LogPath = Join-Path $env:TEMP ("serverhwmon-install-" + [guid]::NewGuid().ToString("N") + ".log")

function Write-InstallLog {
    param([string]$Message)
    $line = ("[{0}] {1}" -f (Get-Date -Format o), $Message)
    try {
        Add-Content -Path $LogPath -Value $line -Encoding UTF8
    } catch {
        # Last-resort fallback to console if filesystem logging is unavailable.
        Write-Host $line
    }
}

function Assert-Admin {
    $currentIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentIdentity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "This installer must be run from an elevated PowerShell session (Run as Administrator)."
    }
}

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
        if ($LASTEXITCODE -ne 0) { throw "git fetch failed for '$InstallDir'." }
        git -C $InstallDir checkout $Branch
        if ($LASTEXITCODE -ne 0) { throw "git checkout '$Branch' failed in '$InstallDir'." }
        git -C $InstallDir pull --ff-only origin $Branch
        if ($LASTEXITCODE -ne 0) { throw "git pull failed for branch '$Branch' in '$InstallDir'." }
    } else {
        if ((Test-Path $InstallDir) -and (Get-ChildItem -LiteralPath $InstallDir -Force -ErrorAction SilentlyContinue | Select-Object -First 1)) {
            throw "InstallDir '$InstallDir' exists and is not a git repository. Remove it or choose a different -InstallDir."
        }
        $parent = Split-Path -Parent $InstallDir
        if (-not (Test-Path $parent)) {
            New-Item -ItemType Directory -Path $parent | Out-Null
        }
        git clone --branch $Branch $RepoUrl $InstallDir
        if ($LASTEXITCODE -ne 0) { throw "git clone failed from '$RepoUrl' branch '$Branch' into '$InstallDir'." }
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
    & $venvPython -m pip install --no-cache-dir psutil pyserial
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

$steps = @(
    @{ Label = "Checking privileges"; Action = { Assert-Admin } },
    @{ Label = "Checking prerequisites"; Action = { Assert-Command git } },
    @{ Label = "Syncing repository"; Action = { Clone-OrUpdateRepo } },
    @{ Label = "Preparing local config"; Action = { Ensure-LocalConfig } },
    @{ Label = "Setting up Python environment"; Action = { Setup-PythonEnv } },
    @{ Label = "Cleaning install artifacts"; Action = { Cleanup-InstallArtifacts } },
    @{ Label = "Installing startup task"; Action = { Install-StartupTask } }
)

try {
    Write-InstallLog "Installer started."
    Write-InstallLog ("PowerShell version: " + $PSVersionTable.PSVersion.ToString())
    Write-InstallLog ("InstallDir: " + $InstallDir)
    Write-InstallLog ("RepoUrl: " + $RepoUrl + " Branch: " + $Branch)

    for ($i = 0; $i -lt $steps.Count; $i++) {
        $percent = [int](($i + 1) * 100 / $steps.Count)
        $stepLabel = $steps[$i].Label
        Write-Host ("[{0}%] {1}" -f $percent, $stepLabel)
        Write-Progress -Activity "Server HWMon Install" -Status $steps[$i].Label -PercentComplete $percent
        Write-InstallLog ("BEGIN step: " + $stepLabel)
        try {
            # Execute directly so thrown exceptions reliably flow into catch.
            & $steps[$i].Action
            Write-InstallLog ("END step: " + $stepLabel + " (ok)")
        } catch {
            Write-InstallLog ("FAILED step: " + $stepLabel)
            Write-InstallLog ("Exception: " + $_.Exception.Message)
            if ($_.ScriptStackTrace) {
                Write-InstallLog ("Stack: " + $_.ScriptStackTrace)
            }
            throw
        }
    }
    Write-Progress -Activity "Server HWMon Install" -Completed
    Remove-Item -LiteralPath $LogPath -Force -ErrorAction SilentlyContinue
    Write-Host "Install successful."
} catch {
    Write-Progress -Activity "Server HWMon Install" -Completed
    Write-Host "Install failed. Log: $LogPath"
    Write-Host ("Error: " + $_.Exception.Message)
    Write-InstallLog ("INSTALL FAILED: " + $_.Exception.Message)
    if (Test-Path $LogPath) {
        Write-Host "---- Last log lines ----"
        Get-Content -Path $LogPath -Tail 120
        Write-Host "------------------------"
    }
    exit 1
}
