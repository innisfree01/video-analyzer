#!/bin/bash
#
# Stop MediaMTX and all FFmpeg streaming processes.
#
PID_DIR="/tmp/uvc_streaming_pids"

echo "=== Stopping streaming services ==="

if [ -d "$PID_DIR" ]; then
    for pidfile in "$PID_DIR"/*.pid; do
        [ -f "$pidfile" ] || continue
        pid=$(cat "$pidfile")
        name=$(basename "$pidfile" .pid)
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            echo "  Stopped ${name} (PID ${pid})"
        fi
        rm -f "$pidfile"
    done
    rmdir "$PID_DIR" 2>/dev/null || true
fi

# Safety: kill any remaining MediaMTX / FFmpeg processes from our setup
pkill -f "mediamtx.*mediamtx.yml" 2>/dev/null || true
pkill -f "ffmpeg.*uvc_stream" 2>/dev/null || true

echo "Done."
