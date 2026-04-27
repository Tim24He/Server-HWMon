from __future__ import annotations

import importlib
import json
import socket
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from typing import Any

try:
    psutil = importlib.import_module("psutil")
except ImportError as exc:
    raise SystemExit(
        "Missing required dependency 'psutil'. Install it with: python -m pip install psutil"
    ) from exc

import serial
from serial.tools import list_ports


POLL_INTERVAL_SECONDS = 2.0
RECONNECT_DELAY_SECONDS = 5.0
SERIAL_BAUDRATE = 115200

ESP32_KEYWORDS = ("esp32", "espressif", "xiao", "seeed")
ESP32_VID_HINTS = {0x303A, 0x2886}


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

    return round(sum(candidates) / len(candidates), 1)


def scrape_host_stats() -> HostStats:
    return HostStats(
        timestamp_utc=datetime.now(timezone.utc).isoformat(timespec="seconds"),
        hostname=socket.gethostname(),
        ip=_get_primary_ipv4(),
        cpu_load_percent=round(psutil.cpu_percent(interval=None), 1),
        core_temp_c=_choose_core_temp_c(),
    )


def serialize_stats(stats: HostStats) -> str:
    """
    Serial-ready line format.
    The ESP32 side can parse one JSON object per newline.
    """
    payload: dict[str, Any] = asdict(stats)
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


def main() -> None:
    # Prime cpu_percent to avoid a misleading first reading.
    psutil.cpu_percent(interval=None)

    print("Periphery scraper started. Ctrl+C to stop.")
    while True:
        serial_conn = _connect_serial_with_retry()
        try:
            while True:
                stats = scrape_host_stats()
                payload = serialize_stats(stats)
                print(payload, flush=True)
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


if __name__ == "__main__":
    main()
