#!/usr/bin/env bash
set -euo pipefail
LOG_FILE="$(mktemp -t serverhwmon-uninstall-XXXX.log)"
trap 'echo "Uninstall failed. See details below:" >&2; tail -n 80 "${LOG_FILE}" >&2' ERR

# Server HWMon Periphery Uninstaller (Linux)
# Intended usage:
#   bash -c "$(curl -fsSL https://raw.githubusercontent.com/Tim24He/Server-HWMon/main/scripts/uninstall-linux.sh)"
#
# Optional environment overrides:
#   INSTALL_DIR=/opt/server-hwmon
#   SERVICE_NAME=server-hwmon-periphery
#   RUN_USER=serverstat
#   REMOVE_USER=false

INSTALL_DIR="${INSTALL_DIR:-/opt/server-hwmon}"
SERVICE_NAME="${SERVICE_NAME:-server-hwmon-periphery}"
RUN_USER="${RUN_USER:-serverstat}"
REMOVE_USER="${REMOVE_USER:-false}"

require_root() {
  if [[ "${EUID}" -ne 0 ]]; then
    echo "This uninstaller must run as root (use sudo)." >&2
    exit 1
  fi
}

remove_service() {
  local service_file="/etc/systemd/system/${SERVICE_NAME}.service"

  if command -v systemctl >/dev/null 2>&1; then
    if systemctl list-unit-files | grep -q "^${SERVICE_NAME}.service"; then
      systemctl disable --now "${SERVICE_NAME}.service" || true
    fi
  else
    echo "systemctl not found; skipping service stop/disable." >&2
  fi

  if [[ -f "${service_file}" ]]; then
    rm -f "${service_file}"
    if command -v systemctl >/dev/null 2>&1; then
      systemctl daemon-reload
    fi
  fi
}

remove_files() {
  if [[ -d "${INSTALL_DIR}" ]]; then
    rm -rf "${INSTALL_DIR}"
  fi
}

remove_user_if_requested() {
  if [[ "${REMOVE_USER}" != "true" ]]; then
    return
  fi
  if id "${RUN_USER}" >/dev/null 2>&1; then
    userdel "${RUN_USER}" || true
  fi
}

main() {
  local total_steps=4
  local current_step=0
  run_step() {
    local label="$1"
    shift
    current_step=$((current_step + 1))
    local percent=$((current_step * 100 / total_steps))
    local filled=$((percent / 5))
    local bar
    bar="$(printf '%*s' "${filled}" '' | tr ' ' '#')"
    bar="${bar}$(printf '%*s' "$((20 - filled))" '' | tr ' ' '-')"
    echo "[${bar}] ${percent}% ${label}"
    "$@" >>"${LOG_FILE}" 2>&1
  }

  run_step "Checking privileges" require_root
  run_step "Removing service" remove_service
  run_step "Removing installed files" remove_files
  run_step "Removing service user (optional)" remove_user_if_requested

  rm -f "${LOG_FILE}"
  echo "Uninstall successful."
}

main "$@"
