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

EXPORT_INTERVAL_SECONDS = 1.0
RECONNECT_DELAY_SECONDS = 5.0
SERIAL_BAUDRATE = 115200
IP_REFRESH_SECONDS = 30.0
SENSOR_REFRESH_SECONDS = 2.0
LOAD_INTERVAL_SECONDS = 1.0
SERIAL_FORCE_FLUSH = False
ESP32_KEYWORDS = ("esp32", "espressif", "xiao", "seeed")
ESP32_VID_HINTS = {0x303A, 0x2886}

DEFAULT_CONFIG_FILE = Path(__file__).with_name("periphery_config.json")
LOCAL_OVERRIDE_CONFIG_FILE = Path(__file__).with_name("periphery_config.local.json")


@dataclass(slots=True)
class HostStats:
    timestamp_utc: str
    hostname: str
    ip: str
    cpu_load_percent: float
    core_temp_c: float | None
    cpu_fan_rpm: float | None
    net_upload_kbps: float
    net_download_kbps: float


_LAST_NET_COUNTERS: Any = None
_LAST_NET_TS: float | None = None
_CACHED_HOSTNAME: str = socket.gethostname()
_CACHED_IP: str = "127.0.0.1"
_LAST_IP_TS: float | None = None
_CACHED_CORE_TEMP_C: float | None = None
_CACHED_CPU_FAN_RPM: float | None = None
_LAST_SENSOR_TS: float | None = None
_CACHED_CPU_LOAD_PERCENT: float = 0.0
_CACHED_NET_UPLOAD_KBPS: float = 0.0
_CACHED_NET_DOWNLOAD_KBPS: float = 0.0
_LAST_LOAD_TS: float | None = None


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


def _apply_runtime_config(data: dict[str, Any], source_name: str) -> None:
    global EXPORT_INTERVAL_SECONDS
    global RECONNECT_DELAY_SECONDS
    global SERIAL_BAUDRATE
    global IP_REFRESH_SECONDS
    global SENSOR_REFRESH_SECONDS
    global LOAD_INTERVAL_SECONDS
    global SERIAL_FORCE_FLUSH
    global ESP32_KEYWORDS
    global ESP32_VID_HINTS
    global DEBUG_MODE

    if not isinstance(data, dict):
        print(f"{source_name} must contain a JSON object. Ignoring this file.")
        return

    export_interval_seconds = data.get("EXPORT_INTERVAL_SECONDS")
    if not isinstance(export_interval_seconds, (int, float)):
        export_interval_seconds = data.get("POLL_INTERVAL_SECONDS")
    if isinstance(export_interval_seconds, (int, float)) and export_interval_seconds > 0:
        EXPORT_INTERVAL_SECONDS = float(export_interval_seconds)

    reconnect_delay_seconds = data.get("RECONNECT_DELAY_SECONDS")
    if isinstance(reconnect_delay_seconds, (int, float)) and reconnect_delay_seconds > 0:
        RECONNECT_DELAY_SECONDS = float(reconnect_delay_seconds)

    serial_baudrate = data.get("SERIAL_BAUDRATE")
    if isinstance(serial_baudrate, int) and serial_baudrate > 0:
        SERIAL_BAUDRATE = serial_baudrate

    ip_refresh_seconds = data.get("IP_REFRESH_SECONDS")
    if isinstance(ip_refresh_seconds, (int, float)) and ip_refresh_seconds > 0:
        IP_REFRESH_SECONDS = float(ip_refresh_seconds)

    sensor_refresh_seconds = data.get("SENSOR_REFRESH_SECONDS")
    if isinstance(sensor_refresh_seconds, (int, float)) and sensor_refresh_seconds > 0:
        SENSOR_REFRESH_SECONDS = float(sensor_refresh_seconds)

    load_interval_seconds = data.get("LOAD_INTERVAL_SECONDS")
    if isinstance(load_interval_seconds, (int, float)) and load_interval_seconds > 0:
        LOAD_INTERVAL_SECONDS = float(load_interval_seconds)

    serial_force_flush = data.get("SERIAL_FORCE_FLUSH")
    if isinstance(serial_force_flush, bool):
        SERIAL_FORCE_FLUSH = serial_force_flush

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


def _load_runtime_config() -> None:
    for config_file in (DEFAULT_CONFIG_FILE, LOCAL_OVERRIDE_CONFIG_FILE):
        if not config_file.exists():
            continue
        try:
            data = json.loads(config_file.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            print(f"Failed to load {config_file.name}: {exc}. Ignoring this file.")
            continue
        _apply_runtime_config(data, config_file.name)


def _choose_core_temp_c() -> float | None:
    """Return representative CPU temperature in C, or None."""
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
    core_temp_c, cpu_fan_rpm = _get_cached_slow_sensors()
    ip = _get_cached_ip()
    cpu_load_percent, net_upload_kbps, net_download_kbps = _get_cached_load_metrics()
    return HostStats(
        timestamp_utc=datetime.now(timezone.utc).isoformat(timespec="seconds"),
        hostname=_CACHED_HOSTNAME,
        ip=ip,
        cpu_load_percent=cpu_load_percent,
        core_temp_c=core_temp_c,
        cpu_fan_rpm=cpu_fan_rpm,
        net_upload_kbps=net_upload_kbps,
        net_download_kbps=net_download_kbps,
    )


def serialize_stats(stats: HostStats) -> str:
    """Serialize one JSON object per line for the ESP32."""
    payload: dict[str, Any] = asdict(stats)
    payload["cpu_load_percent"] = int(round(float(payload["cpu_load_percent"])))
    if payload["core_temp_c"] is not None:
        payload["core_temp_c"] = int(round(float(payload["core_temp_c"])))
    if payload["cpu_fan_rpm"] is not None:
        payload["cpu_fan_rpm"] = int(round(float(payload["cpu_fan_rpm"])))
    payload["net_upload_kbps"] = int(round(float(payload["net_upload_kbps"])))
    payload["net_download_kbps"] = int(round(float(payload["net_download_kbps"])))
    return json.dumps(payload, separators=(",", ":"))


def _choose_cpu_fan_rpm() -> float | None:
    """Return representative CPU fan RPM, or None."""
    try:
        fans_by_source = psutil.sensors_fans()
    except (AttributeError, NotImplementedError):
        return None

    cpu_candidates: list[float] = []
    fallback_candidates: list[float] = []
    for source_name, entries in fans_by_source.items():
        source_text = (source_name or "").lower()
        for entry in entries:
            if entry.current is None:
                continue
            rpm = float(entry.current)
            fallback_candidates.append(rpm)
            label_text = (entry.label or "").lower()
            if any(
                marker in f"{source_text} {label_text}"
                for marker in ("cpu", "proc", "processor")
            ):
                cpu_candidates.append(rpm)

    selected = cpu_candidates if cpu_candidates else fallback_candidates
    if not selected:
        return None
    return float(round(sum(selected) / len(selected)))


def _choose_network_kbps() -> tuple[float, float]:
    """Return upload/download throughput in KB/s."""
    global _LAST_NET_COUNTERS
    global _LAST_NET_TS

    now_ts = time.time()
    current = psutil.net_io_counters()
    if _LAST_NET_COUNTERS is None or _LAST_NET_TS is None:
        _LAST_NET_COUNTERS = current
        _LAST_NET_TS = now_ts
        return 0.0, 0.0

    elapsed = max(now_ts - _LAST_NET_TS, 1e-6)
    bytes_sent_delta = max(current.bytes_sent - _LAST_NET_COUNTERS.bytes_sent, 0)
    bytes_recv_delta = max(current.bytes_recv - _LAST_NET_COUNTERS.bytes_recv, 0)

    _LAST_NET_COUNTERS = current
    _LAST_NET_TS = now_ts

    upload_kbps = bytes_sent_delta / elapsed / 1024.0
    download_kbps = bytes_recv_delta / elapsed / 1024.0
    return float(round(upload_kbps)), float(round(download_kbps))


def _get_cached_ip() -> str:
    global _CACHED_IP
    global _LAST_IP_TS

    now_ts = time.time()
    if _LAST_IP_TS is None or (now_ts - _LAST_IP_TS) >= IP_REFRESH_SECONDS:
        _CACHED_IP = _get_primary_ipv4()
        _LAST_IP_TS = now_ts
    return _CACHED_IP


def _get_cached_slow_sensors() -> tuple[float | None, float | None]:
    global _CACHED_CORE_TEMP_C
    global _CACHED_CPU_FAN_RPM
    global _LAST_SENSOR_TS

    now_ts = time.time()
    if _LAST_SENSOR_TS is None or (now_ts - _LAST_SENSOR_TS) >= SENSOR_REFRESH_SECONDS:
        _CACHED_CORE_TEMP_C = _choose_core_temp_c()
        _CACHED_CPU_FAN_RPM = _choose_cpu_fan_rpm()
        _LAST_SENSOR_TS = now_ts
    return _CACHED_CORE_TEMP_C, _CACHED_CPU_FAN_RPM


def _get_cached_load_metrics() -> tuple[float, float, float]:
    global _CACHED_CPU_LOAD_PERCENT
    global _CACHED_NET_UPLOAD_KBPS
    global _CACHED_NET_DOWNLOAD_KBPS
    global _LAST_LOAD_TS

    now_ts = time.time()
    if _LAST_LOAD_TS is None or (now_ts - _LAST_LOAD_TS) >= LOAD_INTERVAL_SECONDS:
        _CACHED_CPU_LOAD_PERCENT = float(round(psutil.cpu_percent(interval=None)))
        _CACHED_NET_UPLOAD_KBPS, _CACHED_NET_DOWNLOAD_KBPS = _choose_network_kbps()
        _LAST_LOAD_TS = now_ts

    return _CACHED_CPU_LOAD_PERCENT, _CACHED_NET_UPLOAD_KBPS, _CACHED_NET_DOWNLOAD_KBPS


def _prime_runtime_caches() -> None:
    global _CACHED_HOSTNAME
    _CACHED_HOSTNAME = socket.gethostname()
    _get_cached_ip()
    _get_cached_slow_sensors()
    _get_cached_load_metrics()


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
        time.sleep(EXPORT_INTERVAL_SECONDS)


def _run_serial_loop() -> None:
    print("Periphery scraper started. Ctrl+C to stop.")
    while True:
        serial_conn = _connect_serial_with_retry()
        try:
            while True:
                stats = scrape_host_stats()
                payload = serialize_stats(stats)
                serial_conn.write((payload + "\n").encode("utf-8"))
                if SERIAL_FORCE_FLUSH:
                    serial_conn.flush()
                time.sleep(EXPORT_INTERVAL_SECONDS)
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
    _load_runtime_config()
    _prime_runtime_caches()

    if DEBUG_MODE:
        _run_debug_loop()
        return

    _run_serial_loop()


if __name__ == "__main__":
    main()
