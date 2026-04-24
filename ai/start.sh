#!/bin/bash
#
# Start the AI event-understanding stack on the AGX.
#
# Components launched (each as a background process, PIDs written to ai/pids/):
#   1. api_server.py    — FastAPI on :8000
#   2. event_engine.py  — pulls 4 RTSP streams, runs MOG2, writes events
#   3. vlm_worker.py    — analyses pending events with Qwen2.5-VL via Ollama
#
# The summary job is *not* started here; it is intended to be triggered
# either by cron at 23:30 or manually via:
#   python summary_job.py --date $(date +%F)
#
# Usage:
#   bash ai/start.sh           # uses ai/.venv if present, else system python3
#
set -e

AI_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$AI_DIR"

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

if ! curl -sf http://localhost:11434/api/tags >/dev/null; then
    echo "[ai] WARN: ollama not reachable at http://localhost:11434"
    echo "[ai]       VLM/LLM calls will fail until 'sudo systemctl start ollama'"
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
launch event_engine   event_engine.py
launch vlm_worker     vlm_worker.py

echo
echo "[ai] all components running. Tail logs with:"
echo "    tail -F $LOG_DIR/*.out"
echo "[ai] API:    http://$(hostname -I | awk '{print $1}'):8000/health"
echo "[ai] Stop:   bash ai/stop.sh"
