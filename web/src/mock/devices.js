/**
 * Mock 设备数据
 *
 * 在没有后端 API 的阶段，前端用这份假数据来开发和调试 UI。
 * 后续接入真实后端时，只需把数据来源从这里切换到 API 请求。
 *
 * 数据结构设计:
 * - Device (设备): 代表一台物理设备（如 AGX Orin），有 IP 地址和多个通道
 * - Channel (通道): 代表设备上的一路视频流，对应 MediaMTX 的一个 stream path
 */

const defaultAgxIp =
  (typeof window !== 'undefined' && window.location?.hostname && window.location.hostname !== 'localhost'
    ? window.location.hostname
    : '192.168.3.34')

export const mockDevices = [
  {
    id: 'device-agx-1',
    name: 'AGX Orin #1',
    ip: defaultAgxIp,
    status: 'online',
    type: 'jetson',
    mediamtxPort: 8889,
    channels: [
      { id: 'cam0', name: 'Sensor 0 - CH0', streamPath: 'cam0', resolution: '1920x1080', codec: 'H.264' },
      { id: 'cam1', name: 'Sensor 0 - CH1', streamPath: 'cam1', resolution: '1920x1080', codec: 'H.264' },
      { id: 'cam2', name: 'Sensor 1 - CH0', streamPath: 'cam2', resolution: '1920x1080', codec: 'H.264' },
      { id: 'cam3', name: 'Sensor 1 - CH1', streamPath: 'cam3', resolution: '1920x1080', codec: 'H.264' },
    ]
  },
  {
    id: 'device-ipc-1',
    name: 'IPC Camera #1',
    ip: '192.168.3.100',
    status: 'offline',
    type: 'ipc',
    mediamtxPort: 8889,
    channels: [
      { id: 'ch0', name: '主码流', streamPath: 'ipc1_main', resolution: '2560x1440', codec: 'H.265' },
      { id: 'ch1', name: '子码流', streamPath: 'ipc1_sub', resolution: '640x480', codec: 'H.264' },
    ]
  },
  {
    id: 'device-ipc-2',
    name: 'IPC Camera #2',
    ip: '192.168.3.101',
    status: 'offline',
    type: 'ipc',
    mediamtxPort: 8889,
    channels: [
      { id: 'ch0', name: '主码流', streamPath: 'ipc2_main', resolution: '1920x1080', codec: 'H.265' },
    ]
  }
]

export const mockEvents = [
  { id: 1, type: 'motion', deviceId: 'device-agx-1', channelId: 'cam0', time: '2026-04-13 14:32:15', message: '检测到运动目标', severity: 'warning' },
  { id: 2, type: 'offline', deviceId: 'device-ipc-1', channelId: null, time: '2026-04-13 13:15:00', message: '设备离线', severity: 'error' },
  { id: 3, type: 'online', deviceId: 'device-agx-1', channelId: null, time: '2026-04-13 10:00:05', message: '设备上线', severity: 'info' },
  { id: 4, type: 'record', deviceId: 'device-agx-1', channelId: 'cam1', time: '2026-04-13 09:30:00', message: '开始录像', severity: 'info' },
  { id: 5, type: 'alert', deviceId: 'device-agx-1', channelId: 'cam2', time: '2026-04-13 08:45:22', message: '入侵告警', severity: 'error' },
  { id: 6, type: 'motion', deviceId: 'device-agx-1', channelId: 'cam3', time: '2026-04-13 07:12:33', message: '检测到运动目标', severity: 'warning' },
]
