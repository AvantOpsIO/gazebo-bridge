#!/bin/bash
set -euo pipefail
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
export CAMERA_TOPIC="${CAMERA_TOPIC:-/camera}"

cleanup() {
  if [[ -n "${SIM_PID:-}" ]]; then
    kill "${SIM_PID}" 2>/dev/null || true
    wait "${SIM_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

gz sim -r -s /worlds/test_camera.sdf &
SIM_PID=$!

echo "Waiting for ${CAMERA_TOPIC} (gz.msgs.Image)..."
for i in $(seq 1 120); do
  if gz topic -i -t "${CAMERA_TOPIC}" 2>/dev/null | grep -q 'gz.msgs.Image'; then
    echo "Camera topic ready."
    break
  fi
  if ! kill -0 "${SIM_PID}" 2>/dev/null; then
    echo "gz sim exited unexpectedly."
    exit 1
  fi
  sleep 1
  if [[ "$i" -eq 120 ]]; then
    echo "Timeout waiting for camera topic."
    exit 1
  fi
done

cd /app
exec ./build/gazebo_bridge
