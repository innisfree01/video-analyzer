<!--
  设备树组件 - 拖拽源 (Drag Source)

  知识点 - HTML5 Drag & Drop 详解:
  ================================
  浏览器原生支持拖放操作，整个过程分为几个阶段：

  [拖拽源 (Drag Source)]
  1. dragstart: 用户开始拖拽时触发
     - 在此事件中设置要传递的数据: event.dataTransfer.setData(type, data)
     - 设置拖拽效果: event.dataTransfer.effectAllowed = 'copy'
  2. drag: 拖拽过程中持续触发（通常不需要处理）
  3. dragend: 拖拽结束时触发（无论是否成功放置）

  [放置目标 (Drop Target)] → 见 VideoCell.vue
  4. dragenter: 拖拽物进入目标区域
  5. dragover: 拖拽物在目标上方移动（必须 preventDefault 才能接受放置）
  6. drop: 用户松开鼠标，完成放置
  7. dragleave: 拖拽物离开目标区域

  数据流: dragstart 设置数据 → drop 读取数据 → 更新 Store → 视频格子播放
-->
<script setup>
import { useDeviceStore } from '@/stores/devices'
import { ref } from 'vue'

const deviceStore = useDeviceStore()

const expandedDevices = ref(new Set())

function toggleDevice(deviceId) {
  if (expandedDevices.value.has(deviceId)) {
    expandedDevices.value.delete(deviceId)
  } else {
    expandedDevices.value.add(deviceId)
  }
}

/**
 * 拖拽开始 - 将通道信息序列化为 JSON 传递
 * dataTransfer 是拖放 API 的数据载体，类似剪贴板
 */
function onDragStart(event, device, channel) {
  const dragData = {
    deviceId: device.id,
    channelId: channel.id,
    streamPath: channel.streamPath,
    deviceIp: device.ip,
    mediamtxPort: device.mediamtxPort,
    label: `${device.name} / ${channel.name}`,
  }

  event.dataTransfer.setData('application/json', JSON.stringify(dragData))
  event.dataTransfer.effectAllowed = 'copy'
}
</script>

<template>
  <div class="device-tree">
    <div v-if="deviceStore.loading" class="tree-loading">
      加载中...
    </div>

    <div v-else-if="deviceStore.devices.length === 0" class="tree-empty">
      暂无设备
    </div>

    <div v-else>
      <div
        v-for="device in deviceStore.devices"
        :key="device.id"
        class="device-node"
      >
        <!-- 设备节点（可展开/折叠） -->
        <div
          class="device-header"
          :class="{ expanded: expandedDevices.has(device.id) }"
          @click="toggleDevice(device.id)"
        >
          <span class="expand-icon">
            {{ expandedDevices.has(device.id) ? '▼' : '▶' }}
          </span>
          <span
            class="status-dot"
            :class="device.status"
          ></span>
          <span class="device-name">{{ device.name }}</span>
          <span class="device-ip">{{ device.ip }}</span>
        </div>

        <!-- 通道列表（可拖拽） -->
        <div
          v-show="expandedDevices.has(device.id)"
          class="channel-list"
        >
          <div
            v-for="channel in device.channels"
            :key="channel.id"
            class="channel-item"
            :class="{ disabled: device.status !== 'online' }"
            :draggable="device.status === 'online'"
            @dragstart="onDragStart($event, device, channel)"
          >
            <span class="channel-icon">🎥</span>
            <div class="channel-info">
              <span class="channel-name">{{ channel.name }}</span>
              <span class="channel-meta">{{ channel.resolution }} · {{ channel.codec }}</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.device-tree {
  font-size: 13px;
}
.tree-loading, .tree-empty {
  padding: 20px;
  text-align: center;
  color: var(--text-muted);
  font-size: 13px;
}
.device-node {
  margin-bottom: 2px;
}
.device-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 10px;
  border-radius: 6px;
  cursor: pointer;
  transition: background 0.15s;
}
.device-header:hover {
  background: var(--bg-tertiary);
}
.expand-icon {
  font-size: 10px;
  color: var(--text-muted);
  width: 14px;
  flex-shrink: 0;
}
.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}
.status-dot.online {
  background: var(--success);
  box-shadow: 0 0 4px var(--success);
}
.status-dot.offline {
  background: var(--text-muted);
}
.device-name {
  flex: 1;
  font-weight: 500;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.device-ip {
  font-size: 11px;
  color: var(--text-muted);
  flex-shrink: 0;
}
.channel-list {
  padding-left: 20px;
}
.channel-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 10px;
  border-radius: 6px;
  cursor: grab;
  transition: all 0.15s;
  border: 1px solid transparent;
}
.channel-item:hover {
  background: var(--bg-tertiary);
  border-color: var(--accent);
}
.channel-item:active {
  cursor: grabbing;
}
.channel-item.disabled {
  opacity: 0.4;
  cursor: not-allowed;
}
.channel-icon {
  font-size: 14px;
  flex-shrink: 0;
}
.channel-info {
  display: flex;
  flex-direction: column;
  min-width: 0;
}
.channel-name {
  font-size: 13px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.channel-meta {
  font-size: 11px;
  color: var(--text-muted);
}
</style>
