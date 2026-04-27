from __future__ import annotations

import json
import socket
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from typing import Any

import psutil


POLL_INTERVAL_SECONDS = 2.0


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


def main() -> None:
    # Prime cpu_percent to avoid a misleading first reading.
    psutil.cpu_percent(interval=None)

    print("Periphery scraper started (debug mode). Ctrl+C to stop.")
    while True:
        stats = scrape_host_stats()
        print(serialize_stats(stats), flush=True)
        time.sleep(POLL_INTERVAL_SECONDS)


if __name__ == "__main__":
    main()
