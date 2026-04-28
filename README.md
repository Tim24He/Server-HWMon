# Server Stat UI

Server Stat UI is a two-part system for showing host machine telemetry on a small I2C OLED connected to a Seeed XIAO ESP32S3.

- `periphery/` runs on the host PC, scrapes hardware stats, and streams JSON over USB serial.
- `esp32HW/` runs on the ESP32, decodes incoming JSON, and renders a UI on a 128x64 OLED.

## Features

- Host telemetry scraper:
  - CPU load percentage (integer, rounded)
  - Core/package temperature in Celsius (integer, rounded, or `null` if unavailable)
  - Hostname and primary outbound IPv4
- Robust serial protocol:
  - One JSON object per line
  - Graceful handling of missing or extra JSON fields
- OLED dashboard:
  - Dynamic text centering for hostname and IP
  - Data values update on every valid incoming packet
  - Offline screen shown when data is stale
- Runtime config on host via `periphery/periphery_config.json`

## Repository Layout

- `periphery/`
  - `main.py` host scraper + serial sender
  - `periphery_config.json` runtime config file
  - `pyproject.toml` / `uv.lock` Python environment
- `esp32HW/`
  - `src/main.cpp` firmware
  - `platformio.ini` PlatformIO environment and libraries
  - `fonts/README.md` notes for optional custom font conversion

## Hardware

- Board: Seeed XIAO ESP32S3
- Display: 0.96" I2C OLED (128x64)

Recommended wiring on XIAO ESP32S3:

- `SDA` -> `D4`
- `SCL` -> `D5`
- `GND` -> `GND`
- `VCC` -> `3V3` (recommended)

## Data Flow

1. Host script samples system stats.
2. Script serializes them into compact JSON.
3. JSON is sent as newline-delimited frames over USB serial.
4. ESP32 reads line frames, decodes selected fields, and updates OLED.
5. If valid frames stop arriving for a timeout window, firmware switches to an offline screen.

## Host Setup (Python + uv)

From repository root:

```powershell
uv sync --project periphery
uv run --project periphery python periphery/main.py
```

Or from inside `periphery/`:

```powershell
uv sync
uv run python main.py
```

### Host Runtime Configuration

Edit `periphery/periphery_config.json`:

- `DEBUG_MODE` (`true`/`false`)
- `POLL_INTERVAL_SECONDS`
- `RECONNECT_DELAY_SECONDS`
- `SERIAL_BAUDRATE`
- `ESP32_KEYWORDS`
- `ESP32_VID_HINTS`

`DEBUG_MODE: true` prints JSON to console instead of opening serial.

## Firmware Setup (PlatformIO)

From repository root:

```powershell
pio run -d esp32HW
pio run -d esp32HW -t upload
pio device monitor -d esp32HW
```

Or from inside `esp32HW/`:

```powershell
pio run
pio run -t upload
pio device monitor
```

## Serial Payload Format

Each line is a JSON object:

```json
{
  "timestamp_utc": "2026-04-28T19:40:11+00:00",
  "hostname": "MY-PC",
  "ip": "192.168.1.100",
  "cpu_load_percent": 37,
  "core_temp_c": 64
}
```

Notes:

- `cpu_load_percent` is sent as an integer.
- `core_temp_c` is sent as an integer or `null`.

## Fonts

The current layout uses built-in U8g2 fonts.  
If you want custom `.bdf` fonts, see `esp32HW/fonts/README.md` for conversion workflow.

## Current Behavior Summary

- Firmware is tuned for production display updates:
  - I2C clock set to 400kHz
  - Display refresh triggered by valid incoming packets
  - Serial logging minimized/removed in runtime path
- Offline mode appears automatically when data becomes stale.

## Troubleshooting

- No data on OLED:
  - Verify USB serial connection and baud (`115200` by default).
  - Confirm host script detects the ESP32 port.
  - Check `DEBUG_MODE` is `false` if expecting serial output.
- OLED not responding:
  - Recheck I2C wiring and panel address/controller compatibility.
  - Verify power rail and ground continuity.
- Build issues:
  - Ensure PlatformIO and required toolchains are installed.
  - Rebuild after library updates in `platformio.ini`.
