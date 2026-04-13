#!/bin/bash
#
# Start MediaMTX + transcoding pipelines (GStreamer NVENC HW or FFmpeg fallback)
# Reads H.265 from named pipes, transcodes to H.264, pushes to MediaMTX via RTSP.
#
# Usage:
#   bash start.sh                       # default: transcode H.265 → H.264
#   bash start.sh --passthrough         # no transcode, push H.265 directly
#   bash start.sh --pipe-dir /path      # custom pipe directory
#   bash start.sh --max-hw-streams 4    # max concurrent NVENC sessions (default: 4)
#
set -e

PIPE_DIR="/tmp/uvc_stream"
MEDIAMTX_BIN="/opt/mediamtx/mediamtx"
MEDIAMTX_CONF="$(cd "$(dirname "$0")" && pwd)/mediamtx.yml"
RTSP_BASE="rtsp://localhost:8554"
PASSTHROUGH=0
PID_DIR="/tmp/uvc_streaming_pids"
MAX_HW_STREAMS=4

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
        --passthrough)    PASSTHROUGH=1 ;;
        --pipe-dir)       PIPE_DIR="$2"; shift ;;
        --max-hw-streams) MAX_HW_STREAMS="$2"; shift ;;
        *)                echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

# ─── Detect H.264 encoder ────────────────────
detect_encoder() {
    if gst-inspect-1.0 nvv4l2h264enc &>/dev/null; then
        echo "gst_nvenc"
    elif ffmpeg -encoders 2>/dev/null | grep -q "h264_nvmpi"; then
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

# ─── Stop any existing instances ──────────────
echo "Stopping any existing instances..."
bash "$(dirname "$0")/stop.sh" 2>/dev/null || true
sleep 1

mkdir -p "$PID_DIR"

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

# ─── Start transcoding pipelines ─────────────
if [ "$PASSTHROUGH" -eq 0 ]; then
    ENCODER=$(detect_encoder)
    echo ""
    echo "=== H.264 encoder: $ENCODER (max ${MAX_HW_STREAMS} HW streams) ==="
fi

echo ""
echo "=== Starting streams ==="
HW_COUNT=0
for entry in "${STREAMS[@]}"; do
    PIPE_NAME="${entry%%:*}"
    RTSP_PATH="${entry##*:}"
    PIPE_FILE="${PIPE_DIR}/${PIPE_NAME}"

    if [ ! -e "$PIPE_FILE" ]; then
        echo "WARN: Pipe $PIPE_FILE not found, skipping $RTSP_PATH"
        continue
    fi

    if [ "$PASSTHROUGH" -eq 1 ]; then
        gst-launch-1.0 -q \
            fdsrc fd=0 \
            ! h265parse \
            ! "video/x-h265,framerate=30/1" \
            ! rtspclientsink location="${RTSP_BASE}/${RTSP_PATH}" protocols=tcp \
        < "$PIPE_FILE" &
        ENC_LABEL="passthrough"
    elif [ "$ENCODER" = "gst_nvenc" ] && [ "$HW_COUNT" -lt "$MAX_HW_STREAMS" ]; then
        gst-launch-1.0 -q \
            fdsrc fd=0 \
            ! h265parse \
            ! "video/x-h265,framerate=30/1" \
            ! nvv4l2decoder \
            ! nvv4l2h264enc \
                bitrate=2000000 \
                maxperf-enable=true \
                preset-level=1 \
                insert-sps-pps=true \
                iframeinterval=30 \
            ! h264parse config-interval=1 \
            ! rtspclientsink location="${RTSP_BASE}/${RTSP_PATH}" protocols=tcp \
        < "$PIPE_FILE" &
        HW_COUNT=$((HW_COUNT + 1))
        ENC_LABEL="gst_nvenc"
        sleep 2
    else
        ffmpeg -nostdin -hide_banner -loglevel warning \
            -fflags nobuffer -flags low_delay \
            -f hevc -i "$PIPE_FILE" \
            -c:v libx264 -b:v 2000k -preset ultrafast -tune zerolatency \
            -f rtsp -rtsp_transport tcp \
            "${RTSP_BASE}/${RTSP_PATH}" &
        ENC_LABEL="libx264"
    fi

    STRPID=$!
    echo "$STRPID" > "${PID_DIR}/stream_${RTSP_PATH}.pid"
    echo "  ${RTSP_PATH}: pipe=${PIPE_FILE}  PID=${STRPID}  enc=${ENC_LABEL}"
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
