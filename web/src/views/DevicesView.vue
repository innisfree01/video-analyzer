<!--
  设备管理页面

  知识点 - v-model 双向绑定:
  ================================
  v-model 是 Vue 的语法糖，让表单元素和数据保持同步：
  - <input v-model="name"> 等价于:
    <input :value="name" @input="name = $event.target.value">
  - 用户输入时自动更新数据，数据变化时自动更新输入框
  - 这是 MVVM (Model-View-ViewModel) 模式的核心体现

  知识点 - 条件渲染 v-if / v-show:
  - v-if: 条件为 false 时，DOM 元素完全不存在（适合不常切换的场景）
  - v-show: 条件为 false 时，DOM 存在但 display: none（适合频繁切换）
-->
<script setup>
import { ref, reactive } from 'vue'
import { useDeviceStore } from '@/stores/devices'

const deviceStore = useDeviceStore()
const showAddDialog = ref(false)

const newDevice = reactive({
  name: '',
  ip: '',
  mediamtxPort: 8889,
  type: 'jetson',
  channelCount: 4,
})

function openAddDialog() {
  newDevice.name = ''
  newDevice.ip = ''
  newDevice.mediamtxPort = 8889
  newDevice.type = 'jetson'
  newDevice.channelCount = 4
  showAddDialog.value = true
}

function addDevice() {
  if (!newDevice.name || !newDevice.ip) return

  const channels = Array.from({ length: newDevice.channelCount }, (_, i) => ({
    id: `ch${i}`,
    name: `Channel ${i}`,
    streamPath: `cam${i}`,
    resolution: '1920x1080',
    codec: 'H.264',
  }))

  deviceStore.addDevice({
    id: `device-${Date.now()}`,
    name: newDevice.name,
    ip: newDevice.ip,
    status: 'offline',
    type: newDevice.type,
    mediamtxPort: newDevice.mediamtxPort,
    channels,
  })

  showAddDialog.value = false
}

function removeDevice(deviceId) {
  if (confirm('确定要删除该设备吗？')) {
    deviceStore.removeDevice(deviceId)
  }
}

function toggleStatus(device) {
  const newStatus = device.status === 'online' ? 'offline' : 'online'
  deviceStore.updateDeviceStatus(device.id, newStatus)
}
</script>

<template>
  <div class="devices-page">
    <!-- 工具栏 -->
    <div class="devices-toolbar">
      <h2 class="page-title">设备管理</h2>
      <div class="toolbar-stats">
        <span class="stat">
          <span class="stat-dot online"></span>
          在线 {{ deviceStore.onlineDevices.length }}
        </span>
        <span class="stat">
          <span class="stat-dot offline"></span>
          离线 {{ deviceStore.offlineDevices.length }}
        </span>
        <span class="stat">
          通道总数 {{ deviceStore.totalChannels }}
        </span>
      </div>
      <button class="btn-add" @click="openAddDialog">
        + 添加设备
      </button>
    </div>

    <!-- 设备列表（卡片样式） -->
    <div class="devices-grid">
      <div
        v-for="device in deviceStore.devices"
        :key="device.id"
        class="device-card"
        :class="{ offline: device.status === 'offline' }"
      >
        <div class="card-header">
          <div class="card-status">
            <span class="status-indicator" :class="device.status"></span>
            <span class="status-text">{{ device.status === 'online' ? '在线' : '离线' }}</span>
          </div>
          <div class="card-actions">
            <button class="card-btn" title="切换状态" @click="toggleStatus(device)">
              {{ device.status === 'online' ? '⏸' : '▶' }}
            </button>
            <button class="card-btn danger" title="删除" @click="removeDevice(device.id)">
              🗑
            </button>
          </div>
        </div>

        <div class="card-body">
          <h3 class="device-name">{{ device.name }}</h3>
          <div class="device-detail">
            <span class="detail-label">IP 地址</span>
            <span class="detail-value">{{ device.ip }}</span>
          </div>
          <div class="device-detail">
            <span class="detail-label">类型</span>
            <span class="detail-value">{{ device.type }}</span>
          </div>
          <div class="device-detail">
            <span class="detail-label">WHEP 端口</span>
            <span class="detail-value">{{ device.mediamtxPort }}</span>
          </div>
          <div class="device-detail">
            <span class="detail-label">通道数</span>
            <span class="detail-value">{{ device.channels.length }}</span>
          </div>
        </div>

        <div class="card-channels">
          <div
            v-for="ch in device.channels"
            :key="ch.id"
            class="channel-tag"
          >
            {{ ch.name }}
          </div>
        </div>
      </div>

      <!-- 添加设备占位卡片 -->
      <div class="device-card add-card" @click="openAddDialog">
        <div class="add-icon">+</div>
        <div class="add-text">添加新设备</div>
      </div>
    </div>

    <!-- 添加设备对话框 -->
    <Teleport to="body">
      <div v-if="showAddDialog" class="dialog-overlay" @click.self="showAddDialog = false">
        <div class="dialog">
          <h3 class="dialog-title">添加设备</h3>

          <div class="form-field">
            <label>设备名称</label>
            <input v-model="newDevice.name" placeholder="例: AGX Orin #2" />
          </div>
          <div class="form-field">
            <label>IP 地址</label>
            <input v-model="newDevice.ip" placeholder="例: 192.168.3.35" />
          </div>
          <div class="form-field">
            <label>MediaMTX WHEP 端口</label>
            <input v-model.number="newDevice.mediamtxPort" type="number" />
          </div>
          <div class="form-field">
            <label>设备类型</label>
            <select v-model="newDevice.type">
              <option value="jetson">Jetson (AGX/Xavier/Nano)</option>
              <option value="ipc">IPC Camera</option>
              <option value="nvr">NVR</option>
            </select>
          </div>
          <div class="form-field">
            <label>通道数量</label>
            <input v-model.number="newDevice.channelCount" type="number" min="1" max="16" />
          </div>

          <div class="dialog-actions">
            <button class="btn-cancel" @click="showAddDialog = false">取消</button>
            <button class="btn-confirm" @click="addDevice">确认添加</button>
          </div>
        </div>
      </div>
    </Teleport>
  </div>
</template>

<style scoped>
.devices-page {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}
.devices-toolbar {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 12px 16px;
  border-bottom: 1px solid var(--border-color);
  flex-shrink: 0;
}
.page-title {
  font-size: 16px;
  font-weight: 600;
}
.toolbar-stats {
  display: flex;
  gap: 16px;
  flex: 1;
}
.stat {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  color: var(--text-secondary);
}
.stat-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}
.stat-dot.online { background: var(--success); }
.stat-dot.offline { background: var(--text-muted); }
.btn-add {
  padding: 6px 16px;
  border-radius: 6px;
  background: var(--accent);
  color: white;
  font-size: 13px;
  font-weight: 500;
  transition: background 0.15s;
}
.btn-add:hover {
  background: var(--accent-hover);
}

.devices-grid {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
  gap: 16px;
  align-content: start;
}
.device-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: 10px;
  padding: 16px;
  transition: all 0.2s;
}
.device-card:hover {
  border-color: var(--accent);
}
.device-card.offline {
  opacity: 0.6;
}
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}
.card-status {
  display: flex;
  align-items: center;
  gap: 6px;
}
.status-indicator {
  width: 10px;
  height: 10px;
  border-radius: 50%;
}
.status-indicator.online {
  background: var(--success);
  box-shadow: 0 0 6px var(--success);
}
.status-indicator.offline {
  background: var(--text-muted);
}
.status-text {
  font-size: 12px;
  color: var(--text-muted);
}
.card-actions {
  display: flex;
  gap: 4px;
}
.card-btn {
  width: 28px;
  height: 28px;
  border-radius: 6px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  transition: background 0.15s;
}
.card-btn:hover {
  background: var(--bg-tertiary);
}
.card-btn.danger:hover {
  background: rgba(239,68,68,0.2);
}

.card-body {
  margin-bottom: 12px;
}
.device-name {
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 10px;
}
.device-detail {
  display: flex;
  justify-content: space-between;
  padding: 4px 0;
  font-size: 13px;
}
.detail-label {
  color: var(--text-muted);
}
.detail-value {
  color: var(--text-primary);
  font-weight: 500;
}

.card-channels {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  padding-top: 12px;
  border-top: 1px solid var(--border-color);
}
.channel-tag {
  padding: 2px 8px;
  border-radius: 4px;
  background: var(--bg-tertiary);
  font-size: 11px;
  color: var(--text-secondary);
}

.add-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  border-style: dashed;
  cursor: pointer;
  min-height: 200px;
}
.add-card:hover {
  background: var(--bg-tertiary);
}
.add-icon {
  font-size: 32px;
  color: var(--text-muted);
}
.add-text {
  font-size: 14px;
  color: var(--text-muted);
}

/* 对话框样式 */
.dialog-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}
.dialog {
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: 12px;
  padding: 24px;
  width: 420px;
  max-width: 90vw;
}
.dialog-title {
  font-size: 18px;
  font-weight: 600;
  margin-bottom: 20px;
}
.form-field {
  margin-bottom: 14px;
}
.form-field label {
  display: block;
  font-size: 13px;
  color: var(--text-muted);
  margin-bottom: 6px;
}
.form-field input,
.form-field select {
  width: 100%;
  padding: 8px 12px;
  border-radius: 6px;
  border: 1px solid var(--border-color);
  background: var(--bg-tertiary);
  color: var(--text-primary);
  font-size: 14px;
}
.form-field input:focus,
.form-field select:focus {
  outline: none;
  border-color: var(--accent);
}
.dialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 20px;
}
.btn-cancel {
  padding: 8px 16px;
  border-radius: 6px;
  background: var(--bg-tertiary);
  font-size: 14px;
}
.btn-cancel:hover {
  background: var(--bg-hover);
}
.btn-confirm {
  padding: 8px 16px;
  border-radius: 6px;
  background: var(--accent);
  color: white;
  font-size: 14px;
  font-weight: 500;
}
.btn-confirm:hover {
  background: var(--accent-hover);
}
</style>
