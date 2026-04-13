<!--
  单个视频格子 - 拖放目标 (Drop Target) + WebRTC 播放器

  知识点 - 拖放目标的事件处理:
  ================================
  要让一个 DOM 元素成为合法的放置目标，需要处理以下事件：

  1. @dragover.prevent
     - 默认情况下 DOM 元素不接受拖放（浏览器会显示"禁止"图标）
     - 必须调用 event.preventDefault() 来允许放置
     - Vue 的 .prevent 修饰符等价于 event.preventDefault()

  2. @dragenter / @dragleave
     - 用于实现视觉反馈（如高亮边框）
     - dragenter: 拖拽物进入 → 添加高亮样式
     - dragleave: 拖拽物离开 → 移除高亮样式

  3. @drop
     - 放置完成，从 dataTransfer 取出数据
     - 更新 Pinia Store → 触发 WebRTC 连接

  知识点 - Vue 的 watch() 与 immediate:
  ================================
  watch(source, callback, { immediate: true })
  - 监听响应式数据变化
  - immediate: true 表示立即执行一次（不等数据变化）
  - 这里用 watch 监听 cell 数据变化，自动启停 WebRTC 播放
-->
<script setup>
import { ref, watch, computed } from 'vue'
import { useVideoGridStore } from '@/stores/videoGrid'
import { useWhepPlayer } from '@/composables/useWhepPlayer'

const props = defineProps({
  cellIndex: { type: Number, required: true },
  cell: { type: Object, required: true },
})

const gridStore = useVideoGridStore()
const { videoRef, status, errorMsg, play, stop } = useWhepPlayer()

const isDragOver = ref(false)
const isSelected = computed(() => gridStore.selectedCellIndex === props.cellIndex)

const statusConfig = {
  idle: { class: 'offline', label: '空闲' },
  connecting: { class: 'connecting', label: '连接中...' },
  live: { class: 'live', label: 'LIVE' },
  error: { class: 'error', label: '错误' },
}

// 监听 cell 数据变化，自动控制播放
watch(
  () => props.cell.streamPath,
  (newPath) => {
    if (newPath && props.cell.deviceIp && props.cell.mediamtxPort) {
      play(props.cell.deviceIp, props.cell.mediamtxPort, newPath)
    } else {
      stop()
    }
  },
  { immediate: true }
)

function onDragOver(event) {
  event.preventDefault()
  event.dataTransfer.dropEffect = 'copy'
  isDragOver.value = true
}

function onDragLeave() {
  isDragOver.value = false
}

function onDrop(event) {
  event.preventDefault()
  isDragOver.value = false

  try {
    const rawData = event.dataTransfer.getData('application/json')
    if (!rawData) return
    const data = JSON.parse(rawData)
    gridStore.assignChannel(props.cellIndex, data)
  } catch (err) {
    console.error('拖放数据解析失败:', err)
  }
}

function onClearCell() {
  gridStore.clearCell(props.cellIndex)
}

function onSelectCell() {
  gridStore.selectCell(props.cellIndex)
}
</script>

<template>
  <div
    class="video-cell"
    :class="{
      'drag-over': isDragOver,
      'selected': isSelected,
      'has-stream': cell.streamPath,
    }"
    @dragover="onDragOver"
    @dragenter="onDragOver"
    @dragleave="onDragLeave"
    @drop="onDrop"
    @click="onSelectCell"
  >
    <!-- 视频元素 -->
    <video
      ref="videoRef"
      autoplay
      muted
      playsinline
      class="video-element"
    ></video>

    <!-- 空状态提示 -->
    <div v-if="!cell.streamPath" class="empty-state">
      <div class="empty-icon">📺</div>
      <div class="empty-text">拖拽设备通道到此处</div>
      <div class="empty-hint">从左侧设备树拖入</div>
    </div>

    <!-- 信息叠加层 -->
    <div v-if="cell.streamPath" class="cell-overlay">
      <div class="overlay-top">
        <span class="cell-label">{{ cell.label }}</span>
        <span class="status-badge" :class="statusConfig[status]?.class">
          {{ statusConfig[status]?.label }}
        </span>
      </div>
      <div class="overlay-bottom">
        <span v-if="errorMsg" class="error-text">{{ errorMsg }}</span>
        <button class="close-btn" title="关闭" @click.stop="onClearCell">✕</button>
      </div>
    </div>

    <!-- 拖拽高亮 -->
    <div v-if="isDragOver" class="drag-highlight">
      <div class="drag-text">放置到此处</div>
    </div>
  </div>
</template>

<style scoped>
.video-cell {
  position: relative;
  background: #0a0e14;
  border: 2px solid transparent;
  border-radius: 6px;
  overflow: hidden;
  transition: border-color 0.2s;
  cursor: pointer;
}
.video-cell.selected {
  border-color: var(--accent);
}
.video-cell.drag-over {
  border-color: var(--accent);
  border-style: dashed;
}
.video-cell.has-stream {
  background: #000;
}

.video-element {
  width: 100%;
  height: 100%;
  object-fit: contain;
  display: block;
}

.empty-state {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  color: var(--text-muted);
}
.empty-icon {
  font-size: 32px;
  opacity: 0.3;
}
.empty-text {
  font-size: 13px;
}
.empty-hint {
  font-size: 11px;
  opacity: 0.5;
}

.cell-overlay {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  pointer-events: none;
}
.overlay-top {
  padding: 8px 10px;
  background: linear-gradient(180deg, rgba(0,0,0,0.7) 0%, transparent 100%);
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.cell-label {
  font-size: 12px;
  font-weight: 600;
  color: white;
  text-shadow: 0 1px 2px rgba(0,0,0,0.8);
}
.status-badge {
  padding: 2px 8px;
  border-radius: 10px;
  font-size: 10px;
  font-weight: 700;
}
.status-badge.live { background: var(--success); color: white; }
.status-badge.connecting { background: var(--warning); color: #000; }
.status-badge.error { background: var(--danger); color: white; }
.status-badge.offline { background: #555; color: #ccc; }

.overlay-bottom {
  padding: 8px 10px;
  background: linear-gradient(0deg, rgba(0,0,0,0.5) 0%, transparent 100%);
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
}
.error-text {
  font-size: 11px;
  color: var(--danger);
}
.close-btn {
  pointer-events: all;
  width: 24px;
  height: 24px;
  border-radius: 50%;
  background: rgba(255,255,255,0.15);
  color: white;
  font-size: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: background 0.15s;
  margin-left: auto;
}
.close-btn:hover {
  background: var(--danger);
}

.drag-highlight {
  position: absolute;
  inset: 0;
  background: rgba(59, 130, 246, 0.15);
  border: 2px dashed var(--accent);
  border-radius: 4px;
  display: flex;
  align-items: center;
  justify-content: center;
}
.drag-text {
  color: var(--accent);
  font-size: 14px;
  font-weight: 600;
}
</style>
