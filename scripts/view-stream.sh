#!/usr/bin/env bash
set -euo pipefail
HOST="${RTSP_HOST:-127.0.0.1}"
PORT="${RTSP_PORT:-8554}"
TRANSPORT="${RTSP_TRANSPORT:-udp}"
exec ffplay -fflags nobuffer -flags low_delay -rtsp_transport "${TRANSPORT}" "rtsp://${HOST}:${PORT}/stream"
