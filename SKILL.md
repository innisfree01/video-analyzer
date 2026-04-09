# Video Analyzer Skill

🍃 **Orin AGX 多路视频流分析系统**

## 功能概览

- 📹 **多摄像头实时监控** - 同时监控多路 UVC 摄像头视频流
- 🏥 **健康检查** - 检测摄像头在线状态、帧率、分辨率
- 🚨 **异常告警** - 自动记录故障并推送通知（Feishu）
- 📊 **健康报告** - 生成摄像头可用性统计报表

## 目录结构

```
/home/nvidia/.openclaw/skills/video-analyzer/
├── SKILL.md              # 本文件
├── scripts/
│   ├── stream_monitor.py     # 主监控脚本
│   ├── frame_capture.py      # 定时抓拍脚本
│   ├── health_check.py       # 健康检查脚本
│   └── send_alert.py         # Feishu 告警推送
├── config/
│   └── streams.yaml          # 摄像头配置
└── logs/
    ├── health.log            # 健康日志
    ├── snapshots/            # 抓拍图片
    └── alerts.log            # 告警记录
```

## 快速开始

### 1. 安装依赖

```bash
pip3 install opencv-python numpy pyyaml requests
```

### 2. 配置摄像头

编辑 `config/streams.yaml`，设置你的 UVC 摄像头：

```yaml
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
```

### 3. 运行监控

```bash
# 方法 1: 直接运行
python3 scripts/stream_monitor.py

# 方法 2: 后台守护进程
nohup python3 scripts/stream_monitor.py > logs/monitor.log 2>&1 &

# 方法 3: 健康检查模式
python3 scripts/health_check.py
```

## 使用方法

### 实时监控

```bash
# 连续监控所有摄像头
python3 scripts/stream_monitor.py --mode continuous

# 单次检查
python3 scripts/stream_monitor.py --mode one-shot
```

### 定时抓拍

```bash
# 每 5 分钟抓拍一次
python3 scripts/frame_capture.py --interval 300
```

### 健康报告

```bash
# 生成今日健康报告
python3 scripts/health_check.py --report today
```

## 告警配置

设置 Feishu Webhook URL 后自动推送告警：

```bash
export FEISHU_WEBHOOK_URL="https://open.feishu.cn/open-apis/bot/v2/hook/xxx"
```

## 查看日志

```bash
# 查看实时监控日志
tail -f logs/monitor.log

# 查看健康日志
tail -f logs/health.log

# 查看告警记录
tail -f logs/alerts.log
```

## 系统服务（可选）

```bash
# 创建 systemd 服务
sudo systemctl enable video-analyzer
sudo systemctl start video-analyzer
sudo systemctl status video-analyzer
```

## 注意事项

- ⚠️ Orin AGX 有多个 UVC 摄像头时，确保 `/dev/video0-3` 都存在
- ⚠️ 如果摄像头频繁掉线，检查 USB 供电是否充足
- ⚠️ 监控进程会占用一定 CPU/GPU 资源，建议限制 FPS

## 扩展方向

- 🤖 物体检测（YOLO）
- 🎭 行为识别（MediaPipe）
- 📊 数据可视化 Dashboard
- 📈 长期趋势分析

---

_由万叶云间创建 🍃_
