#!/usr/bin/env bash
set -euo pipefail
LOG_FILE="$(mktemp -t serverhwmon-install-XXXX.log)"
trap 'echo "Install failed. See details below:" >&2; tail -n 80 "${LOG_FILE}" >&2' ERR

# Server HWMon Periphery Installer (Linux)
# Intended usage:
#   bash -c "$(curl -fsSL https://raw.githubusercontent.com/<owner>/<repo>/main/scripts/install-linux.sh)"
#
# Optional environment overrides:
#   REPO_URL=https://github.com/<owner>/<repo>.git
#   BRANCH=main
#   INSTALL_DIR=/opt/server-hwmon
#   SERVICE_NAME=server-hwmon-periphery
#   RUN_USER=serverstat

REPO_URL="${REPO_URL:-https://github.com/Tim24He/Server-HWMon.git}"
BRANCH="${BRANCH:-main}"
INSTALL_DIR="${INSTALL_DIR:-/opt/server-hwmon}"
SERVICE_NAME="${SERVICE_NAME:-server-hwmon-periphery}"
RUN_USER="${RUN_USER:-serverstat}"

assert_systemd() {
  if ! command -v systemctl >/dev/null 2>&1; then
    echo "systemd is required on Linux for this installer. Install manually on non-systemd hosts." >&2
    exit 1
  fi
}

require_root() {
  if [[ "${EUID}" -ne 0 ]]; then
    echo "This installer must run as root (use sudo)." >&2
    exit 1
  fi
}

validate_repo_url() {
  if [[ -z "${REPO_URL}" ]]; then
    echo "REPO_URL is empty. Export REPO_URL with your GitHub repo URL first." >&2
    exit 1
  fi
}

ensure_local_config() {
  local periphery_dir="${INSTALL_DIR}/periphery"
  local local_cfg="${periphery_dir}/periphery_config.local.json"
  local example_cfg="${periphery_dir}/periphery_config.local.example.json"

  if [[ -f "${local_cfg}" ]]; then
    return
  fi

  if [[ -f "${example_cfg}" ]]; then
    cp "${example_cfg}" "${local_cfg}"
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

  if command -v yum >/dev/null 2>&1; then
    yum install -y git curl python3 python3-pip
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

  if command -v apk >/dev/null 2>&1; then
    apk add --no-cache git curl python3 py3-pip py3-virtualenv
    return
  fi

  echo "Unsupported package manager. Install git, curl, python3, python3-venv manually." >&2
  exit 1
}

ensure_user() {
  if id "${RUN_USER}" >/dev/null 2>&1; then
    return
  fi
  local nologin_shell="/usr/sbin/nologin"
  if [[ ! -x "${nologin_shell}" ]]; then
    nologin_shell="/usr/bin/nologin"
  fi
  if [[ ! -x "${nologin_shell}" ]]; then
    nologin_shell="/bin/false"
  fi
  useradd --system --create-home --shell "${nologin_shell}" "${RUN_USER}"
}

clone_or_update_repo() {
  if [[ -d "${INSTALL_DIR}/.git" ]]; then
    git -C "${INSTALL_DIR}" remote set-url origin "${REPO_URL}"
    git -C "${INSTALL_DIR}" fetch --depth 1 origin "${BRANCH}"
    git -C "${INSTALL_DIR}" checkout -B "${BRANCH}" "origin/${BRANCH}"
    git -C "${INSTALL_DIR}" sparse-checkout init --cone
    git -C "${INSTALL_DIR}" sparse-checkout set periphery scripts
  else
    mkdir -p "$(dirname "${INSTALL_DIR}")"
    git clone --depth 1 --filter=blob:none --sparse --branch "${BRANCH}" "${REPO_URL}" "${INSTALL_DIR}"
    git -C "${INSTALL_DIR}" sparse-checkout set periphery scripts
  fi
}

setup_python_env() {
  local periphery_dir="${INSTALL_DIR}/periphery"
  local venv_dir="${periphery_dir}/.venv"

  python3 -m venv "${venv_dir}"
  "${venv_dir}/bin/python" -m pip install --no-cache-dir --upgrade pip wheel
  "${venv_dir}/bin/pip" install --no-cache-dir psutil pyserial
}

cleanup_install_artifacts() {
  local user_home
  user_home="$(getent passwd "${RUN_USER}" | cut -d: -f6 || true)"
  if [[ -n "${user_home}" && -d "${user_home}/.cache/pip" ]]; then
    rm -rf "${user_home}/.cache/pip"
  fi
  if [[ -d "/root/.cache/pip" ]]; then
    rm -rf /root/.cache/pip
  fi
}

install_service() {
  local periphery_dir="${INSTALL_DIR}/periphery"
  local service_file="/etc/systemd/system/${SERVICE_NAME}.service"

  cat > "${service_file}" <<EOF
[Unit]
Description=Server HWMon Periphery Agent
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${RUN_USER}
Group=${RUN_USER}
WorkingDirectory=${periphery_dir}
ExecStart=${periphery_dir}/.venv/bin/python ${periphery_dir}/periphery_agent.py
Restart=always
RestartSec=3
StandardOutput=null
StandardError=null

[Install]
WantedBy=multi-user.target
EOF

  chown -R "${RUN_USER}:${RUN_USER}" "${INSTALL_DIR}"
  systemctl daemon-reload
  systemctl enable --now "${SERVICE_NAME}.service"
}

main() {
  local total_steps=9
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
  run_step "Checking service manager" assert_systemd
  run_step "Validating configuration" validate_repo_url
  run_step "Installing base packages" install_packages
  run_step "Ensuring service user" ensure_user
  run_step "Syncing repository" clone_or_update_repo
  run_step "Preparing local config" ensure_local_config
  run_step "Setting up Python environment" setup_python_env
  run_step "Cleaning install artifacts" cleanup_install_artifacts
  run_step "Installing and starting service" install_service

  rm -f "${LOG_FILE}"
  echo "Install successful."
}

main "$@"
