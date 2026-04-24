<!--
  事件列表页面（接入 AI 后端）

  功能：
  - 顶部筛选：日期 / 通道 / 严重程度 / 状态
  - 时间线列表：每条事件展示快照、AI 描述、severity 色条、标签
  - 自动 5s 增量轮询新事件（通过 since_ts 参数）
  - 点击播放按钮弹窗：从 MediaMTX playback 拉视频片段

  知识点 - 增量轮询：
  ================
  保留每次返回中最新事件的 ts，下次只请求 since_ts 之后的事件并合并到列表头。
  当切换日期/通道/筛选条件时回退到全量加载。
-->
<script setup>
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import { listEvents, snapshotUrl, clipUrl, reanalyzeEvent } from '@/api/events'

const today = new Date().toISOString().slice(0, 10)

const filterDate = ref(today)
const filterChannel = ref('all')
const filterSeverity = ref('all')
const filterStatus = ref('all')

const events = ref([])
const loading = ref(false)
const errorMsg = ref('')
const pollTimer = ref(null)

const playbackEvent = ref(null) // event being played in modal

const channels = [
  { value: 'all', label: '全部通道' },
  { value: 'cam0', label: 'cam0' },
  { value: 'cam1', label: 'cam1' },
  { value: 'cam2', label: 'cam2' },
  { value: 'cam3', label: 'cam3' },
]

const severities = [
  { value: 'all', label: '全部级别' },
  { value: 'high', label: '高 high' },
  { value: 'medium', label: '中 medium' },
  { value: 'low', label: '低 low' },
]

const statuses = [
  { value: 'all', label: '全部状态' },
  { value: 'pending', label: '待分析' },
  { value: 'analyzed', label: '已分析' },
  { value: 'failed', label: '解析失败' },
]

const severityIcons = {
  high: '\u{1F534}',    // 🔴
  medium: '\u{1F7E1}',  // 🟡
  low: '\u{1F535}',     // 🔵
}

const filterParams = computed(() => ({
  date: filterDate.value,
  channel: filterChannel.value,
  severity: filterSeverity.value,
  status: filterStatus.value,
  limit: 200,
}))

async function loadAll() {
  loading.value = true
  errorMsg.value = ''
  try {
    const data = await listEvents(filterParams.value)
    events.value = data
  } catch (err) {
    errorMsg.value = `加载失败: ${err.message}`
    events.value = []
  } finally {
    loading.value = false
  }
}

async function pollIncremental() {
  if (events.value.length === 0) return
  // only poll for *today* (no point checking historical dates for new entries)
  if (filterDate.value !== today) return
  try {
    const sinceTs = events.value[0].ts
    const fresh = await listEvents({
      since_ts: sinceTs,
      channel: filterChannel.value,
      severity: filterSeverity.value,
      status: filterStatus.value,
      limit: 50,
    })
    if (fresh.length > 0) {
      const known = new Set(events.value.map(e => e.id))
      const merged = [...fresh.filter(e => !known.has(e.id)), ...events.value]
      events.value = merged.slice(0, 500) // hard cap to keep DOM small
    }
  } catch {
    // silent on poll errors; next poll will retry
  }
}

function startPolling() {
  stopPolling()
  pollTimer.value = setInterval(pollIncremental, 5000)
}

function stopPolling() {
  if (pollTimer.value) {
    clearInterval(pollTimer.value)
    pollTimer.value = null
  }
}

async function handleReanalyze(event) {
  try {
    await reanalyzeEvent(event.id)
    await loadAll()
  } catch (err) {
    alert(`重新分析失败: ${err.message}`)
  }
}

function openPlayback(event) {
  playbackEvent.value = event
}

function closePlayback() {
  playbackEvent.value = null
}

const playbackUrl = computed(() => {
  if (!playbackEvent.value) return ''
  return clipUrl(playbackEvent.value.id)
})

watch(filterParams, () => {
  loadAll()
}, { deep: true })

onMounted(() => {
  loadAll()
  startPolling()
})

onUnmounted(() => {
  stopPolling()
})
</script>

<template>
  <div class="events-page">
    <!-- 过滤栏 -->
    <div class="events-toolbar">
      <div class="filter-group">
        <input
          type="date"
          v-model="filterDate"
          class="filter-input"
        />
        <select v-model="filterChannel" class="filter-select">
          <option v-for="c in channels" :key="c.value" :value="c.value">
            {{ c.label }}
          </option>
        </select>
        <select v-model="filterSeverity" class="filter-select">
          <option v-for="s in severities" :key="s.value" :value="s.value">
            {{ s.label }}
          </option>
        </select>
        <select v-model="filterStatus" class="filter-select">
          <option v-for="s in statuses" :key="s.value" :value="s.value">
            {{ s.label }}
          </option>
        </select>
        <button class="btn-refresh" @click="loadAll" :disabled="loading">
          {{ loading ? '加载中…' : '刷新' }}
        </button>
      </div>
      <span class="event-count">
        共 {{ events.length }} 条事件
        <span v-if="filterDate === today" class="live-dot" title="自动每 5s 拉取新事件" />
      </span>
    </div>

    <!-- 错误提示 -->
    <div v-if="errorMsg" class="events-error">{{ errorMsg }}</div>

    <!-- 事件列表 -->
    <div class="events-list">
      <div
        v-for="event in events"
        :key="event.id"
        class="event-card"
        :class="['severity-' + (event.severity || 'unknown'),
                 'status-' + event.status]"
      >
        <!-- 快照轮播 -->
        <div class="event-snapshots">
          <img
            v-for="(_, idx) in event.snapshots"
            :key="idx"
            :src="snapshotUrl(event.id, idx)"
            class="event-thumb"
            :alt="`snapshot ${idx}`"
            loading="lazy"
          />
          <div v-if="event.snapshots.length === 0" class="event-thumb-placeholder">
            无快照
          </div>
        </div>

        <!-- 主体 -->
        <div class="event-body">
          <div class="event-header">
            <span class="severity-icon">{{ severityIcons[event.severity] || '\u26AA' }}</span>
            <span class="event-channel">{{ event.channel }}</span>
            <span class="event-time">{{ event.time }}</span>
            <span class="event-source">{{ event.source }}</span>
            <span
              class="event-status"
              :class="'status-tag-' + event.status"
            >{{ event.status }}</span>
          </div>

          <div class="event-description">
            <template v-if="event.status === 'pending'">
              <span class="placeholder">AI 分析中…</span>
            </template>
            <template v-else-if="event.description">
              {{ event.description }}
            </template>
            <template v-else>
              <span class="placeholder">无描述</span>
            </template>
          </div>

          <div v-if="event.tags && event.tags.length" class="event-tags">
            <span v-for="tag in event.tags" :key="tag" class="tag-chip">
              #{{ tag }}
            </span>
          </div>
        </div>

        <!-- 操作 -->
        <div class="event-actions">
          <button
            class="btn-play"
            :disabled="!event.clip_url"
            @click="openPlayback(event)"
            title="播放回放"
          >▶ 回放</button>
          <button
            v-if="event.status !== 'pending'"
            class="btn-reanalyze"
            @click="handleReanalyze(event)"
            title="重新分析"
          >↻ 重析</button>
        </div>
      </div>

      <div v-if="!loading && events.length === 0" class="events-empty">
        当日暂无事件。可以在 cam0 前面挥挥手测试一下。
      </div>
    </div>

    <!-- 回放弹窗 -->
    <div v-if="playbackEvent" class="modal-mask" @click.self="closePlayback">
      <div class="modal-box">
        <div class="modal-header">
          <span>{{ playbackEvent.channel }} · {{ playbackEvent.time }}</span>
          <button class="modal-close" @click="closePlayback">×</button>
        </div>
        <video
          class="modal-video"
          :src="playbackUrl"
          controls
          autoplay
          playsinline
        />
        <div class="modal-desc" v-if="playbackEvent.description">
          {{ playbackEvent.description }}
        </div>
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
  gap: 16px;
  flex-wrap: wrap;
}
.filter-group {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}
.filter-input,
.filter-select {
  padding: 6px 10px;
  border-radius: 6px;
  border: 1px solid var(--border-color);
  background: var(--bg-tertiary);
  color: var(--text-primary);
  font-size: 13px;
}
.filter-input:focus,
.filter-select:focus { outline: none; border-color: var(--accent); }
.btn-refresh {
  padding: 6px 14px;
  border-radius: 6px;
  background: var(--accent);
  color: white;
  font-size: 13px;
}
.btn-refresh:disabled { opacity: 0.5; cursor: wait; }
.event-count {
  font-size: 12px;
  color: var(--text-muted);
  display: flex;
  align-items: center;
  gap: 6px;
}
.live-dot {
  width: 8px; height: 8px; border-radius: 50%;
  background: var(--success);
  box-shadow: 0 0 6px var(--success);
  animation: pulse 1.5s infinite;
}
@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.4; }
}

.events-error {
  margin: 8px 16px;
  padding: 10px 14px;
  background: rgba(239,68,68,0.1);
  border: 1px solid var(--danger);
  border-radius: 6px;
  color: var(--danger);
  font-size: 13px;
}

.events-list {
  flex: 1;
  overflow-y: auto;
  padding: 12px 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.event-card {
  display: grid;
  grid-template-columns: auto 1fr auto;
  gap: 14px;
  padding: 12px;
  background: var(--bg-secondary);
  border-radius: 8px;
  border-left: 4px solid var(--text-muted);
}
.event-card.severity-high { border-left-color: var(--danger); }
.event-card.severity-medium { border-left-color: var(--warning); }
.event-card.severity-low { border-left-color: var(--accent); }
.event-card.status-failed { opacity: 0.7; }

.event-snapshots {
  display: flex;
  gap: 4px;
  flex-shrink: 0;
}
.event-thumb {
  width: 96px;
  height: 56px;
  object-fit: cover;
  border-radius: 4px;
  background: var(--bg-tertiary);
}
.event-thumb-placeholder {
  width: 96px;
  height: 56px;
  border-radius: 4px;
  background: var(--bg-tertiary);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 11px;
  color: var(--text-muted);
}

.event-body {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.event-header {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 12px;
  color: var(--text-muted);
  flex-wrap: wrap;
}
.severity-icon { font-size: 13px; }
.event-channel {
  color: var(--text-primary);
  font-weight: 600;
}
.event-time {
  font-variant-numeric: tabular-nums;
}
.event-source {
  padding: 1px 6px;
  background: var(--bg-tertiary);
  border-radius: 4px;
  font-size: 10px;
  text-transform: uppercase;
}
.event-status {
  padding: 1px 6px;
  border-radius: 4px;
  font-size: 10px;
}
.status-tag-pending { background: rgba(245,158,11,0.15); color: var(--warning); }
.status-tag-analyzed { background: rgba(34,197,94,0.15); color: var(--success); }
.status-tag-failed { background: rgba(239,68,68,0.15); color: var(--danger); }

.event-description {
  font-size: 14px;
  color: var(--text-primary);
  line-height: 1.5;
}
.placeholder { color: var(--text-muted); font-style: italic; }

.event-tags { display: flex; gap: 6px; flex-wrap: wrap; }
.tag-chip {
  padding: 2px 8px;
  font-size: 11px;
  background: var(--bg-tertiary);
  color: var(--text-secondary);
  border-radius: 10px;
}

.event-actions {
  display: flex;
  flex-direction: column;
  gap: 6px;
  align-items: stretch;
}
.btn-play, .btn-reanalyze {
  padding: 6px 12px;
  font-size: 12px;
  border-radius: 4px;
  background: var(--bg-tertiary);
  color: var(--text-primary);
  white-space: nowrap;
}
.btn-play:hover:not(:disabled),
.btn-reanalyze:hover { background: var(--bg-hover); }
.btn-play:disabled { opacity: 0.4; cursor: not-allowed; }

.events-empty {
  padding: 40px;
  text-align: center;
  color: var(--text-muted);
}

.modal-mask {
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.7);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 100;
}
.modal-box {
  background: var(--bg-secondary);
  border-radius: 8px;
  width: min(900px, 90vw);
  max-height: 90vh;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}
.modal-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 16px;
  border-bottom: 1px solid var(--border-color);
  font-size: 13px;
}
.modal-close {
  font-size: 20px;
  color: var(--text-muted);
  width: 28px;
  height: 28px;
  border-radius: 4px;
}
.modal-close:hover { background: var(--bg-hover); color: var(--text-primary); }
.modal-video {
  width: 100%;
  background: black;
  max-height: 60vh;
}
.modal-desc {
  padding: 12px 16px;
  font-size: 13px;
  border-top: 1px solid var(--border-color);
  color: var(--text-secondary);
}
</style>
