#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="th06-relay"
INSTALL_DIR="/opt/th06-relay"
SERVICE_PORT="${1:-3478}"
LOG_LEVEL="${TH06_RELAY_LOG_LEVEL:-INFO}"
TRAFFIC_LOG_MODE="${TH06_RELAY_TRAFFIC_MODE:-summary}"
TRAFFIC_SUMMARY_INTERVAL="${TH06_RELAY_TRAFFIC_INTERVAL:-1.0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required"
  exit 1
fi

mkdir -p "${INSTALL_DIR}"
cp "${SCRIPT_DIR}/th06_relay_server.py" "${INSTALL_DIR}/th06_relay_server.py"
chmod 755 "${INSTALL_DIR}/th06_relay_server.py"

if command -v systemctl >/dev/null 2>&1; then
  cat >"/etc/systemd/system/${SERVICE_NAME}.service" <<EOF
[Unit]
Description=TH06 UDP relay probe service
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/env python3 ${INSTALL_DIR}/th06_relay_server.py --port ${SERVICE_PORT} --log-level ${LOG_LEVEL} --traffic-log-mode ${TRAFFIC_LOG_MODE} --traffic-summary-interval ${TRAFFIC_SUMMARY_INTERVAL}
Restart=always
RestartSec=2
WorkingDirectory=${INSTALL_DIR}

[Install]
WantedBy=multi-user.target
EOF

  systemctl daemon-reload
  systemctl enable --now "${SERVICE_NAME}.service"
  systemctl --no-pager --full status "${SERVICE_NAME}.service" || true
else
  nohup /usr/bin/env python3 "${INSTALL_DIR}/th06_relay_server.py" --port "${SERVICE_PORT}" --log-level "${LOG_LEVEL}" --traffic-log-mode "${TRAFFIC_LOG_MODE}" --traffic-summary-interval "${TRAFFIC_SUMMARY_INTERVAL}" \
    >"${INSTALL_DIR}/relay.log" 2>&1 &
  echo "${SERVICE_NAME} started with nohup, log: ${INSTALL_DIR}/relay.log"
fi

if command -v ufw >/dev/null 2>&1; then
  ufw allow "${SERVICE_PORT}/udp" || true
fi

echo
echo "Installed ${SERVICE_NAME} on UDP port ${SERVICE_PORT}."
echo "Remember to add a UDP port mapping in your VPS panel as well."
