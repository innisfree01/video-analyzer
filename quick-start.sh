#!/bin/bash
# Video Analyzer Quick Start Script
# 快速启动脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "🍃 万叶云间 - Video Analyzer 快速启动"
echo "======================================"
echo ""

# 检查 Python 依赖
echo "📦 检查依赖..."
python3 -c "import cv2; import yaml" 2>/dev/null || {
    echo "❌ 缺少依赖，正在安装..."
    pip3 install opencv-python numpy pyyaml
}
echo "✅ 依赖检查完成"
echo ""

# 检查配置文件
if [ ! -f "config/streams.yaml" ]; then
    echo "⚠️  配置文件不存在，创建默认配置..."
    mkdir -p config
    cat > config/streams.yaml << 'EOF'
streams:
  - name: uvc_channel_0
    source: "/dev/video0"
    resolution: "1920x1080"
    fps: 30
  - name: uvc_channel_1
    source: "/dev/video1"
    resolution: "1920x1080"
    fps: 30
  - name: uvc_channel_2
    source: "/dev/video2"
    resolution: "1920x1080"
    fps: 30
  - name: uvc_channel_3
    source: "/dev/video3"
    resolution: "1920x1080"
    fps: 30
EOF
    echo "✅ 已创建默认配置文件：config/streams.yaml"
    echo "💡 提示：请根据需要修改摄像头配置"
else
    echo "✅ 配置文件已存在：config/streams.yaml"
fi
echo ""

# 创建日志目录
mkdir -p logs/snapshots
echo "✅ 日志目录已准备"
echo ""

# 显示菜单
echo "请选择运行模式："
echo "1) 健康检查（快速检测所有摄像头）"
echo "2) 连续监控（实时监控状态）"
echo "3) 定时抓拍（每 5 分钟抓拍一次）"
echo "4) 单次抓拍（立即抓拍所有摄像头）"
echo "5) 查看日志"
echo "0) 退出"
echo ""
read -p "请选择 [0-5]: " choice

case $choice in
    1)
        echo ""
        echo "🏥 运行健康检查..."
        python3 scripts/health_check.py
        ;;
    2)
        echo ""
        echo "🔄 启动连续监控 (Ctrl+C 停止)..."
        python3 scripts/stream_monitor.py --mode continuous --interval 5
        ;;
    3)
        echo ""
        echo "📸 启动定时抓拍 (Ctrl+C 停止)..."
        python3 scripts/frame_capture.py --interval 300
        ;;
    4)
        echo ""
        echo "📸 立即抓拍..."
        python3 scripts/frame_capture.py --once
        ;;
    5)
        echo ""
        echo "📄 查看日志 (tail -f):"
        echo "   - 监控日志：logs/monitor.log"
        echo "   - 抓拍日志：logs/capture.log"
        echo "   - 健康日志：logs/health.log"
        read -p "按回车键继续，或按 Ctrl+C 退出"
        ;;
    0)
        echo "👋 退出"
        ;;
    *)
        echo "❌ 无效选择"
        exit 1
        ;;
esac

echo ""
echo "✅ 完成！"
