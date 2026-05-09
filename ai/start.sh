#!/bin/bash
#
# Start the AI event-ingest stack on the AGX.
#
# Components launched (each as a background process, PIDs written to ai/pids/):
#   1. api_server.py    — FastAPI on configured port, receives IPC events
#
# OpenCV local motion detection and VLM analysis are optional. By default,
# IPC/UVC devices own event detection (motion/human/sound), while AGX
# captures one snapshot and stores an analyzed DB row.
#
# The summary job is *not* started here; it is intended to be triggered
# either by cron at 23:30 or manually via:
#   python summary_job.py --date $(date +%F)
#
# Usage:
#   bash ai/start.sh                    # API only: external IPC event ingest
#   bash ai/start.sh --with-opencv      # also run local MOG2 motion detection
#   bash ai/start.sh --with-vlm         # also analyze pending events with VLM
#
set -e

AI_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$AI_DIR"

WITH_OPENCV=0
WITH_VLM=0
while [ $# -gt 0 ]; do
    case "$1" in
        --with-opencv) WITH_OPENCV=1 ;;
        --with-vlm)    WITH_VLM=1 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

PID_DIR="$AI_DIR/pids"
LOG_DIR="$AI_DIR/logs"
mkdir -p "$PID_DIR" "$LOG_DIR"

# ─── Pick python ──────────────────────────────
if [ -x "$AI_DIR/.venv/bin/python" ]; then
    PY="$AI_DIR/.venv/bin/python"
    echo "[ai] using venv: $PY"
else
    PY="python3"
    echo "[ai] using system python3 (consider creating $AI_DIR/.venv)"
fi

# ─── Stop any existing instances ──────────────
bash "$AI_DIR/stop.sh" >/dev/null 2>&1 || true
sleep 1

# ─── Sanity checks ────────────────────────────
DB_PATH=$($PY - <<'PY'
from common import load_config
print(load_config()["storage"]["db_path"])
PY
)
mkdir -p "$(dirname "$DB_PATH")"

if [ "$WITH_VLM" -eq 1 ] && ! curl -sf http://localhost:11434/api/tags >/dev/null; then
    echo "[ai] WARN: ollama not reachable at http://localhost:11434"
    echo "[ai]       VLM calls will fail until ollama is running"
fi

# ─── Launch ───────────────────────────────────
launch() {
    local name="$1"; shift
    nohup "$PY" "$@" >"$LOG_DIR/${name}.out" 2>&1 &
    echo $! > "$PID_DIR/${name}.pid"
    echo "[ai] started $name pid=$(cat "$PID_DIR/${name}.pid") log=$LOG_DIR/${name}.out"
}

launch api_server     api_server.py
sleep 1
if [ "$WITH_OPENCV" -eq 1 ]; then
    launch event_engine   event_engine.py
else
    echo "[ai] skipped event_engine (OpenCV local motion detection disabled)"
fi
if [ "$WITH_VLM" -eq 1 ]; then
    launch vlm_worker     vlm_worker.py
else
    echo "[ai] skipped vlm_worker (external events are stored as analyzed)"
fi

echo
echo "[ai] all components running. Tail logs with:"
echo "    tail -F $LOG_DIR/*.out"
PORT=$($PY - <<'PY'
from common import load_config
print(load_config()["api"]["port"])
PY
)
echo "[ai] API:    http://$(hostname -I | awk '{print $1}'):${PORT}/health"
echo "[ai] Stop:   bash ai/stop.sh"
