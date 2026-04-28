#!/usr/bin/env bash
set -euo pipefail

# Server Stat UI Periphery Installer (Linux)
# Intended usage:
#   bash -c "$(curl -fsSL https://raw.githubusercontent.com/<owner>/<repo>/main/scripts/install-linux.sh)"
#
# Optional environment overrides:
#   REPO_URL=https://github.com/<owner>/<repo>.git
#   BRANCH=main
#   INSTALL_DIR=/opt/server-stat-ui
#   SERVICE_NAME=server-stat-periphery
#   RUN_USER=serverstat

REPO_URL="${REPO_URL:-https://github.com/REPLACE_ME/Server_Stat_UI.git}"
BRANCH="${BRANCH:-main}"
INSTALL_DIR="${INSTALL_DIR:-/opt/server-stat-ui}"
SERVICE_NAME="${SERVICE_NAME:-server-stat-periphery}"
RUN_USER="${RUN_USER:-serverstat}"

require_root() {
  if [[ "${EUID}" -ne 0 ]]; then
    echo "This installer must run as root (use sudo)." >&2
    exit 1
  fi
}

validate_repo_url() {
  if [[ "${REPO_URL}" == *"REPLACE_ME"* ]]; then
    echo "REPO_URL is not set. Export REPO_URL with your GitHub repo URL first." >&2
    echo "Example: REPO_URL=https://github.com/yourname/Server_Stat_UI.git" >&2
    exit 1
  fi
}

install_packages() {
  if command -v apt-get >/dev/null 2>&1; then
    apt-get update
    apt-get install -y git curl python3 python3-venv
    return
  fi

  if command -v dnf >/dev/null 2>&1; then
    dnf install -y git curl python3 python3-pip
    return
  fi

  if command -v pacman >/dev/null 2>&1; then
    pacman -Sy --noconfirm git curl python python-virtualenv
    return
  fi

  if command -v zypper >/dev/null 2>&1; then
    zypper --non-interactive install git curl python3 python3-virtualenv
    return
  fi

  echo "Unsupported package manager. Install git, curl, python3, python3-venv manually." >&2
  exit 1
}

ensure_user() {
  if id "${RUN_USER}" >/dev/null 2>&1; then
    return
  fi
  useradd --system --create-home --shell /usr/sbin/nologin "${RUN_USER}"
}

clone_or_update_repo() {
  if [[ -d "${INSTALL_DIR}/.git" ]]; then
    git -C "${INSTALL_DIR}" fetch --all --prune
    git -C "${INSTALL_DIR}" checkout "${BRANCH}"
    git -C "${INSTALL_DIR}" pull --ff-only origin "${BRANCH}"
  else
    mkdir -p "$(dirname "${INSTALL_DIR}")"
    git clone --branch "${BRANCH}" "${REPO_URL}" "${INSTALL_DIR}"
  fi
}

setup_python_env() {
  local periphery_dir="${INSTALL_DIR}/periphery"
  local venv_dir="${periphery_dir}/.venv"

  python3 -m venv "${venv_dir}"
  "${venv_dir}/bin/python" -m pip install --upgrade pip wheel
  "${venv_dir}/bin/pip" install psutil pyserial serial
}

install_service() {
  local periphery_dir="${INSTALL_DIR}/periphery"
  local service_file="/etc/systemd/system/${SERVICE_NAME}.service"

  cat > "${service_file}" <<EOF
[Unit]
Description=Server Stat UI Periphery Agent
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${RUN_USER}
Group=${RUN_USER}
WorkingDirectory=${periphery_dir}
ExecStart=${periphery_dir}/.venv/bin/python ${periphery_dir}/main.py
Restart=always
RestartSec=3
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

  chown -R "${RUN_USER}:${RUN_USER}" "${INSTALL_DIR}"
  systemctl daemon-reload
  systemctl enable --now "${SERVICE_NAME}.service"
}

main() {
  require_root
  validate_repo_url
  install_packages
  ensure_user
  clone_or_update_repo
  setup_python_env
  install_service
  echo "Install complete."
  echo "Service: ${SERVICE_NAME}.service"
  echo "Status: systemctl status ${SERVICE_NAME}.service"
}

main "$@"
