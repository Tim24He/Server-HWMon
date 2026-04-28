from __future__ import annotations

import json
import socket
import time
from pathlib import Path
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from typing import Any

import psutil
import serial
from serial.tools import list_ports


DEBUG_MODE = False

POLL_INTERVAL_SECONDS = 2.0
RECONNECT_DELAY_SECONDS = 5.0
SERIAL_BAUDRATE = 115200
ESP32_KEYWORDS = ("esp32", "espressif", "xiao", "seeed")
ESP32_VID_HINTS = {0x303A, 0x2886}

LOCAL_CONFIG_FILE = Path(__file__).with_name("periphery_config.json")


@dataclass(slots=True)
class HostStats:
    timestamp_utc: str
    hostname: str
    ip: str
    cpu_load_percent: float
    core_temp_c: float | None


def _get_primary_ipv4() -> str:
    # Standard trick to discover local outbound IP without sending traffic.
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("8.8.8.8", 80))
        return probe.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        probe.close()


def _load_local_runtime_config() -> None:
    global POLL_INTERVAL_SECONDS
    global RECONNECT_DELAY_SECONDS
    global SERIAL_BAUDRATE
    global ESP32_KEYWORDS
    global ESP32_VID_HINTS
    global DEBUG_MODE

    if not LOCAL_CONFIG_FILE.exists():
        return

    try:
        data = json.loads(LOCAL_CONFIG_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"Failed to load {LOCAL_CONFIG_FILE.name}: {exc}. Using defaults.")
        return

    if not isinstance(data, dict):
        print(f"{LOCAL_CONFIG_FILE.name} must contain a JSON object. Using defaults.")
        return

    poll_interval_seconds = data.get("POLL_INTERVAL_SECONDS")
    if isinstance(poll_interval_seconds, (int, float)) and poll_interval_seconds > 0:
        POLL_INTERVAL_SECONDS = float(poll_interval_seconds)

    reconnect_delay_seconds = data.get("RECONNECT_DELAY_SECONDS")
    if isinstance(reconnect_delay_seconds, (int, float)) and reconnect_delay_seconds > 0:
        RECONNECT_DELAY_SECONDS = float(reconnect_delay_seconds)

    serial_baudrate = data.get("SERIAL_BAUDRATE")
    if isinstance(serial_baudrate, int) and serial_baudrate > 0:
        SERIAL_BAUDRATE = serial_baudrate

    esp32_keywords = data.get("ESP32_KEYWORDS")
    if isinstance(esp32_keywords, list):
        filtered_keywords = [item for item in esp32_keywords if isinstance(item, str)]
        if filtered_keywords:
            ESP32_KEYWORDS = tuple(filtered_keywords)

    esp32_vid_hints = data.get("ESP32_VID_HINTS")
    if isinstance(esp32_vid_hints, list):
        parsed_vids: set[int] = set()
        for item in esp32_vid_hints:
            if isinstance(item, int) and item >= 0:
                parsed_vids.add(item)
            elif isinstance(item, str):
                item_text = item.strip().lower()
                if item_text.startswith("0x"):
                    item_text = item_text[2:]
                try:
                    parsed_vids.add(int(item_text, 16))
                except ValueError:
                    continue
        if parsed_vids:
            ESP32_VID_HINTS = parsed_vids

    debug_mode = data.get("DEBUG_MODE")
    if isinstance(debug_mode, bool):
        DEBUG_MODE = debug_mode


def _choose_core_temp_c() -> float | None:
    """
    Return a representative CPU core/package temperature in Celsius.
    On some Windows environments psutil may not expose temperatures; in that
    case this returns None.
    """
    try:
        temps_by_source = psutil.sensors_temperatures(fahrenheit=False)
    except (AttributeError, NotImplementedError):
        return None

    candidates: list[float] = []
    for entries in temps_by_source.values():
        for entry in entries:
            if entry.current is None:
                continue
            label = (entry.label or "").lower()
            # Favor obvious CPU labels when available.
            if any(k in label for k in ("cpu", "core", "package", "tctl", "tdie")):
                candidates.append(float(entry.current))

    if not candidates:
        for entries in temps_by_source.values():
            for entry in entries:
                if entry.current is not None:
                    candidates.append(float(entry.current))

    if not candidates:
        return None

    return float(round(sum(candidates) / len(candidates)))


def scrape_host_stats() -> HostStats:
    return HostStats(
        timestamp_utc=datetime.now(timezone.utc).isoformat(timespec="seconds"),
        hostname=socket.gethostname(),
        ip=_get_primary_ipv4(),
        cpu_load_percent=float(round(psutil.cpu_percent(interval=None))),
        core_temp_c=_choose_core_temp_c(),
    )


def serialize_stats(stats: HostStats) -> str:
    """
    Serial-ready line format.
    The ESP32 side can parse one JSON object per newline.
    """
    payload: dict[str, Any] = asdict(stats)
    payload["cpu_load_percent"] = int(round(float(payload["cpu_load_percent"])))
    if payload["core_temp_c"] is not None:
        payload["core_temp_c"] = int(round(float(payload["core_temp_c"])))
    return json.dumps(payload, separators=(",", ":"))


def _is_likely_esp32_port(port: list_ports.ListPortInfo) -> bool:
    combined = " ".join(
        (
            port.device or "",
            port.description or "",
            port.manufacturer or "",
            port.hwid or "",
            port.product or "",
            port.interface or "",
        )
    ).lower()
    if any(keyword in combined for keyword in ESP32_KEYWORDS):
        return True
    if port.vid is not None and port.vid in ESP32_VID_HINTS:
        return True
    return False


def _find_esp32_port() -> str | None:
    candidates = list_ports.comports()
    for port in candidates:
        if _is_likely_esp32_port(port):
            return port.device
    return None


def _connect_serial_with_retry() -> serial.Serial:
    while True:
        port_name = _find_esp32_port()
        if not port_name:
            print(
                f"No ESP32 USB serial device found. Retrying in {RECONNECT_DELAY_SECONDS:.0f}s..."
            )
            time.sleep(RECONNECT_DELAY_SECONDS)
            continue

        try:
            ser = serial.Serial(port=port_name, baudrate=SERIAL_BAUDRATE, timeout=1.0)
            print(f"Connected to ESP32 on {port_name} @ {SERIAL_BAUDRATE} baud")
            return ser
        except serial.SerialException as exc:
            print(
                f"Failed to open {port_name}: {exc}. Retrying in {RECONNECT_DELAY_SECONDS:.0f}s..."
            )
            time.sleep(RECONNECT_DELAY_SECONDS)


def _run_debug_loop() -> None:
    print("Periphery scraper started in debug mode. Ctrl+C to stop.")
    while True:
        stats = scrape_host_stats()
        payload = serialize_stats(stats)
        print(payload, flush=True)
        time.sleep(POLL_INTERVAL_SECONDS)


def _run_serial_loop() -> None:
    print("Periphery scraper started. Ctrl+C to stop.")
    while True:
        serial_conn = _connect_serial_with_retry()
        try:
            while True:
                stats = scrape_host_stats()
                payload = serialize_stats(stats)
                serial_conn.write((payload + "\n").encode("utf-8"))
                serial_conn.flush()
                time.sleep(POLL_INTERVAL_SECONDS)
        except (serial.SerialException, OSError) as exc:
            print(
                f"Serial connection dropped: {exc}. Reconnecting in {RECONNECT_DELAY_SECONDS:.0f}s..."
            )
            time.sleep(RECONNECT_DELAY_SECONDS)
        finally:
            try:
                serial_conn.close()
            except OSError:
                pass


def main() -> None:
    # Prime cpu_percent to avoid a misleading first reading.
    psutil.cpu_percent(interval=None)
    _load_local_runtime_config()

    if DEBUG_MODE:
        _run_debug_loop()
        return

    _run_serial_loop()


if __name__ == "__main__":
    main()
