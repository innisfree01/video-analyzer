#!/bin/bash
#
# Start MediaMTX + 4x FFmpeg transcoding pipelines
# Reads H.265 from named pipes, transcodes to H.264, pushes to MediaMTX via RTSP.
#
# Usage:
#   bash start.sh                     # default: transcode H.265 → H.264
#   bash start.sh --passthrough       # no transcode, push H.265 directly
#   bash start.sh --pipe-dir /path    # custom pipe directory
#
set -e

PIPE_DIR="/tmp/uvc_stream"
MEDIAMTX_BIN="/opt/mediamtx/mediamtx"
MEDIAMTX_CONF="$(cd "$(dirname "$0")" && pwd)/mediamtx.yml"
RTSP_BASE="rtsp://localhost:8554"
PASSTHROUGH=0
PID_DIR="/tmp/uvc_streaming_pids"

# Stream mapping: pipe_name -> rtsp_path
STREAMS=(
    "sensor0_ch0.h265:cam0"
    "sensor1_ch3.h265:cam1"
    "sensor2_ch6.h265:cam2"
    "sensor3_ch9.h265:cam3"
)

# ─── Parse arguments ──────────────────────────
while [ $# -gt 0 ]; do
    case "$1" in
        --passthrough) PASSTHROUGH=1 ;;
        --pipe-dir)    PIPE_DIR="$2"; shift ;;
        *)             echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

# ─── Detect H.264 encoder ────────────────────
detect_encoder() {
    if ffmpeg -encoders 2>/dev/null | grep -q "h264_nvmpi"; then
        echo "h264_nvmpi"
    elif ffmpeg -encoders 2>/dev/null | grep -q "h264_v4l2m2m"; then
        echo "h264_v4l2m2m"
    else
        echo "libx264"
    fi
}

# ─── Pre-flight checks ───────────────────────
if [ ! -f "$MEDIAMTX_BIN" ]; then
    echo "ERROR: MediaMTX not found at $MEDIAMTX_BIN"
    echo "Run:  bash streaming/setup.sh"
    exit 1
fi

if [ ! -d "$PIPE_DIR" ]; then
    echo "ERROR: Pipe directory $PIPE_DIR not found."
    echo "Start uvc_host first:  sudo ./uvc_host -p"
    exit 1
fi

mkdir -p "$PID_DIR"

# ─── Stop any existing instances ──────────────
echo "Stopping any existing instances..."
bash "$(dirname "$0")/stop.sh" 2>/dev/null || true
sleep 1

# ─── Start MediaMTX ──────────────────────────
echo ""
echo "=== Starting MediaMTX ==="
echo "Config: $MEDIAMTX_CONF"
"$MEDIAMTX_BIN" "$MEDIAMTX_CONF" &
MEDIAMTX_PID=$!
echo "$MEDIAMTX_PID" > "$PID_DIR/mediamtx.pid"
echo "MediaMTX PID: $MEDIAMTX_PID"
sleep 2

if ! kill -0 "$MEDIAMTX_PID" 2>/dev/null; then
    echo "ERROR: MediaMTX failed to start!"
    exit 1
fi

# ─── Start FFmpeg pipelines ──────────────────
if [ "$PASSTHROUGH" -eq 0 ]; then
    ENCODER=$(detect_encoder)
    echo ""
    echo "=== H.264 encoder: $ENCODER ==="
fi

echo ""
echo "=== Starting FFmpeg streams ==="
for entry in "${STREAMS[@]}"; do
    PIPE_NAME="${entry%%:*}"
    RTSP_PATH="${entry##*:}"
    PIPE_FILE="${PIPE_DIR}/${PIPE_NAME}"

    if [ ! -e "$PIPE_FILE" ]; then
        echo "WARN: Pipe $PIPE_FILE not found, skipping $RTSP_PATH"
        continue
    fi

    if [ "$PASSTHROUGH" -eq 1 ]; then
        # H.265 passthrough — no transcoding
        ffmpeg -nostdin -hide_banner -loglevel warning \
            -fflags nobuffer -flags low_delay \
            -f hevc -i "$PIPE_FILE" \
            -c:v copy \
            -f rtsp -rtsp_transport tcp \
            "${RTSP_BASE}/${RTSP_PATH}" &
    else
        # Transcode H.265 → H.264
        EXTRA_OPTS=""
        if [ "$ENCODER" = "libx264" ]; then
            EXTRA_OPTS="-preset ultrafast -tune zerolatency"
        fi
        ffmpeg -nostdin -hide_banner -loglevel warning \
            -fflags nobuffer -flags low_delay \
            -f hevc -i "$PIPE_FILE" \
            -c:v "$ENCODER" -b:v 2000k $EXTRA_OPTS \
            -f rtsp -rtsp_transport tcp \
            "${RTSP_BASE}/${RTSP_PATH}" &
    fi

    FFPID=$!
    echo "$FFPID" > "${PID_DIR}/ffmpeg_${RTSP_PATH}.pid"
    echo "  ${RTSP_PATH}: pipe=${PIPE_FILE}  PID=${FFPID}"
done

echo ""
echo "=== All streams started ==="
echo ""
echo "Access:"
echo "  WebRTC:  http://$(hostname -I | awk '{print $1}'):8889/cam0"
echo "  HLS:     http://$(hostname -I | awk '{print $1}'):8888/cam0"
echo "  RTSP:    rtsp://$(hostname -I | awk '{print $1}'):8554/cam0"
echo "  Web UI:  http://$(hostname -I | awk '{print $1}'):8889"
echo ""
echo "  4-channel view: open streaming/www/index.html in browser"
echo ""
echo "Stop with:  bash streaming/stop.sh"
echo ""

wait
