#!/usr/bin/env bash
set -euo pipefail

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

  if systemctl list-unit-files | grep -q "^${SERVICE_NAME}.service"; then
    systemctl disable --now "${SERVICE_NAME}.service" || true
  fi

  if [[ -f "${service_file}" ]]; then
    rm -f "${service_file}"
    systemctl daemon-reload
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
  require_root
  remove_service
  remove_files
  remove_user_if_requested
  echo "Uninstall complete."
}

main "$@"
