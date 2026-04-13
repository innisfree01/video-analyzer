<!--
  事件列表页面

  知识点 - 响应式数据与 ref():
  ================================
  ref() 是 Vue 3 创建响应式变量的核心 API。
  
  原理：ref() 返回一个 Proxy 包装的对象 { value: xxx }。
  当 .value 被读取时，Vue 记录"谁在用这个数据"（依赖收集）。
  当 .value 被修改时，Vue 通知所有依赖方更新（派发更新）。
  
  在 <script setup> 中需要用 .value 访问，
  但在 <template> 模板中会自动解包（不需要写 .value）。

  知识点 - computed():
  ================================
  computed() 创建计算属性，它的值由其他响应式数据派生。
  特点：
  - 具有缓存机制，依赖不变就不重新计算
  - 返回值是只读的 ref
  - 用于需要根据原始数据计算出展示数据的场景
-->
<script setup>
import { ref, computed } from 'vue'
import { mockEvents } from '@/mock/devices'
import { useDeviceStore } from '@/stores/devices'

const deviceStore = useDeviceStore()
const events = ref(mockEvents)

const filterType = ref('all')
const filterSeverity = ref('all')

const eventTypes = [
  { value: 'all', label: '全部类型' },
  { value: 'motion', label: '运动检测' },
  { value: 'alert', label: '告警' },
  { value: 'online', label: '上线' },
  { value: 'offline', label: '离线' },
  { value: 'record', label: '录像' },
]

const severityLevels = [
  { value: 'all', label: '全部级别' },
  { value: 'info', label: '信息' },
  { value: 'warning', label: '警告' },
  { value: 'error', label: '错误' },
]

const filteredEvents = computed(() => {
  return events.value.filter(ev => {
    if (filterType.value !== 'all' && ev.type !== filterType.value) return false
    if (filterSeverity.value !== 'all' && ev.severity !== filterSeverity.value) return false
    return true
  })
})

function getDeviceName(deviceId) {
  const device = deviceStore.getDeviceById(deviceId)
  return device?.name || deviceId
}

const severityIcons = {
  info: 'ℹ️',
  warning: '⚠️',
  error: '🔴',
}
</script>

<template>
  <div class="events-page">
    <!-- 过滤栏 -->
    <div class="events-toolbar">
      <div class="filter-group">
        <select v-model="filterType" class="filter-select">
          <option v-for="t in eventTypes" :key="t.value" :value="t.value">
            {{ t.label }}
          </option>
        </select>
        <select v-model="filterSeverity" class="filter-select">
          <option v-for="s in severityLevels" :key="s.value" :value="s.value">
            {{ s.label }}
          </option>
        </select>
      </div>
      <span class="event-count">共 {{ filteredEvents.length }} 条事件</span>
    </div>

    <!-- 事件列表 -->
    <div class="events-list">
      <div
        v-for="event in filteredEvents"
        :key="event.id"
        class="event-item"
        :class="'severity-' + event.severity"
      >
        <div class="event-icon">{{ severityIcons[event.severity] || 'ℹ️' }}</div>
        <div class="event-content">
          <div class="event-message">{{ event.message }}</div>
          <div class="event-meta">
            <span class="event-device">{{ getDeviceName(event.deviceId) }}</span>
            <span v-if="event.channelId" class="event-channel">/ {{ event.channelId }}</span>
          </div>
        </div>
        <div class="event-time">{{ event.time }}</div>
        <div class="event-type-badge" :class="'type-' + event.type">
          {{ event.type }}
        </div>
      </div>

      <div v-if="filteredEvents.length === 0" class="events-empty">
        暂无匹配的事件记录
      </div>
    </div>
  </div>
</template>

<style scoped>
.events-page {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}
.events-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  border-bottom: 1px solid var(--border-color);
  flex-shrink: 0;
}
.filter-group {
  display: flex;
  gap: 8px;
}
.filter-select {
  padding: 6px 10px;
  border-radius: 6px;
  border: 1px solid var(--border-color);
  background: var(--bg-tertiary);
  color: var(--text-primary);
  font-size: 13px;
}
.filter-select:focus {
  outline: none;
  border-color: var(--accent);
}
.event-count {
  font-size: 12px;
  color: var(--text-muted);
}

.events-list {
  flex: 1;
  overflow-y: auto;
  padding: 8px 16px;
}
.event-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px;
  border-radius: 8px;
  margin-bottom: 4px;
  transition: background 0.15s;
  border-left: 3px solid transparent;
}
.event-item:hover {
  background: var(--bg-tertiary);
}
.event-item.severity-error {
  border-left-color: var(--danger);
}
.event-item.severity-warning {
  border-left-color: var(--warning);
}
.event-item.severity-info {
  border-left-color: var(--accent);
}

.event-icon {
  font-size: 18px;
  flex-shrink: 0;
}
.event-content {
  flex: 1;
  min-width: 0;
}
.event-message {
  font-size: 14px;
  font-weight: 500;
}
.event-meta {
  font-size: 12px;
  color: var(--text-muted);
  margin-top: 2px;
}
.event-time {
  font-size: 12px;
  color: var(--text-muted);
  white-space: nowrap;
  font-variant-numeric: tabular-nums;
}
.event-type-badge {
  padding: 2px 8px;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  background: var(--bg-tertiary);
  color: var(--text-secondary);
  white-space: nowrap;
}
.event-type-badge.type-alert { background: rgba(239,68,68,0.15); color: var(--danger); }
.event-type-badge.type-motion { background: rgba(245,158,11,0.15); color: var(--warning); }
.event-type-badge.type-online { background: rgba(34,197,94,0.15); color: var(--success); }
.event-type-badge.type-offline { background: rgba(85,102,119,0.2); color: var(--text-muted); }

.events-empty {
  padding: 40px;
  text-align: center;
  color: var(--text-muted);
}
</style>
