#!/bin/bash
#
# Stop all AI components started by start.sh by killing the PIDs
# recorded in ai/pids/. Safe to run multiple times.
#
AI_DIR="$(cd "$(dirname "$0")" && pwd)"
PID_DIR="$AI_DIR/pids"

if [ ! -d "$PID_DIR" ]; then
    echo "[ai] no pid directory, nothing to stop"
    exit 0
fi

for pidfile in "$PID_DIR"/*.pid; do
    [ -f "$pidfile" ] || continue
    name=$(basename "$pidfile" .pid)
    pid=$(cat "$pidfile" 2>/dev/null || true)
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        echo "[ai] stopping $name pid=$pid"
        kill "$pid" 2>/dev/null || true
        # give it up to 5s to exit cleanly
        for _ in 1 2 3 4 5; do
            kill -0 "$pid" 2>/dev/null || break
            sleep 1
        done
        if kill -0 "$pid" 2>/dev/null; then
            echo "[ai] force killing $name pid=$pid"
            kill -9 "$pid" 2>/dev/null || true
        fi
    fi
    rm -f "$pidfile"
done

echo "[ai] done."
