# Server_Stat_UI Monorepo Layout

This repository is split so Python (`uv`) and PlatformIO stay isolated:

- `apps/periphery/`: Python app (`pyproject.toml`, `uv.lock`, local `.venv`)
- `firmware/esp32HW/`: ESP32 firmware (`platformio.ini`, `.pio` build output)

## Python (`uv`) workflow

Run commands from the repo root with `--project`:

```powershell
uv sync --project apps/periphery
uv run --project apps/periphery python main.py
```

Or `cd apps/periphery` and use `uv sync` / `uv run ...` normally.

## PlatformIO workflow

Run from repo root with `-d`:

```powershell
pio run -d firmware/esp32HW
pio device monitor -d firmware/esp32HW
```

Or `cd firmware/esp32HW` and run `pio run` there.
