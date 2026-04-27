# Server_Stat_UI Project Layout

This repository keeps Python (`uv`) and PlatformIO side by side at root:

- `periphery/`: Python app (`pyproject.toml`, `uv.lock`, local `.venv`)
- `esp32HW/`: ESP32 firmware (`platformio.ini`, `.pio` build output)

## Python (`uv`) workflow

Run commands from the repo root with `--project`:

```powershell
uv sync --project periphery
uv run --project periphery python periphery/main.py
```

Or `cd periphery` and use `uv sync` / `uv run ...` normally.

## PlatformIO workflow

Run from repo root with `-d`:

```powershell
pio run -d esp32HW
pio device monitor -d esp32HW
```

Or `cd esp32HW` and run `pio run` there.
