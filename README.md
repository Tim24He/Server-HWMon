# Server HWMon

![Platform](https://img.shields.io/badge/platform-ESP32S3-0A7CFF)
![Host](https://img.shields.io/badge/host-Python-3776AB)
![Display](https://img.shields.io/badge/display-I2C%20OLED-222222)
![Build](https://img.shields.io/badge/build-PlatformIO-F5822A)

Host telemetry on a small OLED via ESP32.

- `periphery/`: Python agent on your host machine
- `esp32HW/`: ESP32 firmware for OLED rendering

## Install

### One-command installer scripts

Run these from an elevated shell:

- Linux: root (or `sudo`)
- Windows: Run PowerShell as Administrator

Without elevated permissions, service/task registration will fail.

Linux:

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Tim24He/Server-HWMon/main/scripts/install-linux.sh)"
```

Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/Tim24He/Server-HWMon/main/scripts/install-windows.ps1 | iex"
```

What these do:

- Clone/update the repo
- Create the periphery virtual environment
- Install Python dependencies
- Register persistent startup runtime

### Manual install

Host agent:

```powershell
uv sync --project periphery
uv run --project periphery python periphery/periphery_agent.py
```

Firmware:

```powershell
pio run -d esp32HW
pio run -d esp32HW -t upload
```

## Architecture

```mermaid
flowchart LR
    A[Host PC: periphery_agent.py] -->|USB Serial JSON| B[ESP32S3 Firmware]
    B --> C[0.96 inch I2C OLED]
```

## Hardware Wiring

- `SDA` -> `D4`
- `SCL` -> `D5`
- `GND` -> `GND`
- `VCC` -> `3V3`

## Configuration

Use `periphery/periphery_config.local.json` for machine-local settings.  
If absent, the agent uses built-in defaults from `periphery_agent.py`.

Common settings:

- `DEBUG_MODE`
- `EXPORT_INTERVAL_SECONDS` (or legacy alias `POLL_INTERVAL_SECONDS`)
- `RECONNECT_DELAY_SECONDS`
- `IP_REFRESH_SECONDS`
- `SENSOR_REFRESH_SECONDS`
- `LOAD_INTERVAL_SECONDS`
- `SERIAL_FORCE_FLUSH`
- `SERIAL_BAUDRATE`

## Notes

- CPU and temperature are sent as rounded integers.
- Firmware switches to an offline screen when valid data is stale.
- Installer scripts are safe to rerun for updates.

## Uninstall

Linux:

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Tim24He/Server-HWMon/main/scripts/uninstall-linux.sh)"
```

Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/Tim24He/Server-HWMon/main/scripts/uninstall-windows.ps1 | iex"
```

Optional flags:

- Linux:
  - `REMOVE_USER=true` to also remove the service account user.
- Windows:
  - `-RemoveInstallDir $false` to keep files and only remove the scheduled task.
