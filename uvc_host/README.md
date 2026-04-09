# UVC Host Controller for NVIDIA Orin AGX

基于国科嵌入式 SDK 接口规范，通过 UVC Extension Unit (XU) 私有协议与 UVC 摄像头通信，实现视频流采集。

## 为什么需要这个程序

UVC 摄像头使用了国科定制的 XU 扩展协议，必须先通过 `UVCIOC_CTRL_QUERY` ioctl 发送特定的 XU 命令打开视频通道，摄像头才会开始推送视频流。普通的 OpenCV / GStreamer 无法直接读取这些摄像头。

## 工作流程

```
Host (Orin AGX)                         Device (UVC Camera)
     |                                        |
     |--- [XU] Set NTP Time ----------------->|
     |--- [XU] Set Host Status -------------->|
     |--- [XU] Get Device Info -------------->|
     |<-- [XU] Device Version ----------------|
     |--- [XU] Open Video Channel ----------->|  (switch_on=1, 设置分辨率/编码/帧率)
     |--- [V4L2] STREAMON ------------------->|
     |                                        |
     |<== [DY66 Header + Video Frame] ========|  (持续推送视频流)
     |<== [DY66 Header + Video Frame] ========|
     |<== [DY66 Header + Detect Event] =======|  (侦测事件，附带 JPG 快照)
     |<== [DY66 Header + GPS Data] ===========|
     |         ...                            |
     |                                        |
     |--- [XU] Close Video Channel ---------->|  (switch_on=0)
     |--- [V4L2] STREAMOFF ------------------>|
```

## 编译

### 在 Orin AGX 上直接编译

```bash
cd uvc_host
chmod +x build.sh
./build.sh
```

### 手动编译

```bash
cd uvc_host
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 使用

```bash
# 默认：同时从 /dev/video0 和 /dev/video2 采集
sudo ./build/uvc_host

# 保存视频流到文件
sudo ./build/uvc_host -s

# 自定义分辨率和编码
sudo ./build/uvc_host -d /dev/video0 -W 1280 -H 720 -e h264 -f 25 -b 1000 -s

# 单个摄像头
sudo ./build/uvc_host -d /dev/video0 -s

# 保存侦测事件的 JPG 快照
sudo ./build/uvc_host -s -S

# 查看帮助
./build/uvc_host -h
```

### 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-d, --device` | 设备路径（可多次指定） | `/dev/video0` + `/dev/video2` |
| `-W, --width` | 视频宽度 | 1920 |
| `-H, --height` | 视频高度 | 1080 |
| `-f, --fps` | 帧率 | 30 |
| `-b, --bitrate` | 码率 (Kbps) | 2000 |
| `-e, --encoding` | 编码：h264, h265, yuv, mjpeg | h265 |
| `-o, --output` | 输出目录 | ./output |
| `-s, --save` | 保存视频流到文件 | 否 |
| `-S, --save-detect` | 保存侦测事件快照 | 否 |

## 输出文件

保存视频流后，可以用 ffplay/ffmpeg 播放：

```bash
# 播放 H.265 裸流
ffplay -f hevc output/cam0_ch0.h265

# 播放 H.264 裸流
ffplay -f h264 output/cam0_ch0.h264

# 转封装为 MP4
ffmpeg -f hevc -i output/cam0_ch0.h265 -c copy output/cam0.mp4
```

## 协议说明

本程序实现了嵌入式 SDK 接口规范 V2.7 中定义的以下功能：

### Host -> Device (XU 命令)

- **0x01** 视频参数：打开/关闭通道、设置分辨率、编码、帧率、码率
- **0x02** NTP 时间同步
- **0x03** 音频参数：打开/关闭音频通道
- **0x06** 通用控制：对讲、白光灯、电子防抖等
- **0x07** 获取设备信息
- **0x0B** Host 状态通知

### Device -> Host (DY66 帧)

通过 V4L2 streaming 接收，每帧带有 `DY66` 魔术头：

| 帧类型 | ID | 说明 |
|--------|-----|------|
| 视频 | 1 | H.264/H.265/MJPEG 编码帧 |
| 侦测事件 | 2 | 附带 JPG 快照 |
| 配网事件 | 3 | WiFi SSID/密码/Token |
| GPS-RTK | 4 | GPS 定位数据 |
| IPC 复位 | 5 | 设备复位通知 |
| GSensor | 6 | 陀螺仪/加速度数据 |
| 音频 | 7 | 音频帧数据 |

## 依赖

- Linux 内核 V4L2 + UVC 驱动（Orin AGX 自带）
- CMake >= 3.10
- GCC / aarch64 工具链

## 通道编号

```
Channel = sensor_index * 3 + channel_index

sensor0: channel 0, 1, 2
sensor1: channel 3, 4, 5
sensor2: channel 6, 7, 8
sensor3: channel 9, 10, 11
```

默认 `/dev/video0` 对应 sensor0 channel 0，`/dev/video2` 对应 sensor1 channel 3。
