<!--
  日报总结页面

  功能：
  - 顶部日期选择 + "已生成日报"列表
  - 4 个 stat 卡：总事件数 / high / medium / low
  - 中部：narrative（LLM 生成的整体叙述）
  - 中部：highlights 列表（按 severity + 时间排序）
  - 下部：通道活跃度横向条形图（纯 CSS）

  没有日报时，后端会返回 live-aggregate（仅有计数，无 narrative/highlights），
  前端在 narrative 区域提示"今日暂未生成日报，可手动触发"。
-->
<script setup>
import { computed, onMounted, ref, watch } from 'vue'
import { getSummary, listSummaryDates } from '@/api/events'

const today = new Date().toISOString().slice(0, 10)
const date = ref(today)
const summary = ref(null)
const summaryDates = ref([])
const loading = ref(false)
const errorMsg = ref('')

const severityColors = {
  high: 'var(--danger)',
  medium: 'var(--warning)',
  low: 'var(--accent)',
}

const channelStatsArr = computed(() => {
  if (!summary.value || !summary.value.channel_stats) return []
  const entries = Object.entries(summary.value.channel_stats)
  const max = Math.max(1, ...entries.map(([, v]) => v))
  return entries
    .sort((a, b) => b[1] - a[1])
    .map(([ch, count]) => ({ ch, count, pct: Math.round((count / max) * 100) }))
})

const generatedTime = computed(() => {
  if (!summary.value || !summary.value.generated_at) return ''
  return new Date(summary.value.generated_at).toLocaleString('zh-CN')
})

const isLiveAggregate = computed(() => {
  return summary.value && !summary.value.generated_at
})

async function load() {
  loading.value = true
  errorMsg.value = ''
  try {
    summary.value = await getSummary(date.value)
  } catch (err) {
    errorMsg.value = `加载失败: ${err.message}`
    summary.value = null
  } finally {
    loading.value = false
  }
}

async function loadDates() {
  try {
    const r = await listSummaryDates()
    summaryDates.value = r.dates || []
  } catch {
    summaryDates.value = []
  }
}

watch(date, load)

onMounted(() => {
  load()
  loadDates()
})
</script>

<template>
  <div class="summary-page">
    <!-- 顶部工具栏 -->
    <div class="summary-toolbar">
      <div class="filter-group">
        <input type="date" v-model="date" :max="today" class="filter-input" />
        <select
          v-if="summaryDates.length > 0"
          @change="(e) => date = e.target.value"
          :value="date"
          class="filter-select"
        >
          <option disabled value="">已生成日报…</option>
          <option v-for="d in summaryDates" :key="d" :value="d">{{ d }}</option>
        </select>
        <button class="btn-refresh" @click="load" :disabled="loading">
          {{ loading ? '加载中…' : '刷新' }}
        </button>
      </div>
      <span v-if="generatedTime" class="generated-info">
        {{ summary.model }} · 生成于 {{ generatedTime }}
      </span>
    </div>

    <!-- 错误 -->
    <div v-if="errorMsg" class="summary-error">{{ errorMsg }}</div>

    <div class="summary-content" v-if="summary">
      <!-- 实时聚合提示 -->
      <div v-if="isLiveAggregate" class="aggregate-banner">
        当日尚未生成正式日报（仅显示实时聚合统计）。
        <code>python ai/summary_job.py --date {{ date }}</code> 可手动触发。
      </div>

      <!-- Stat 卡 -->
      <div class="stat-row">
        <div class="stat-card">
          <div class="stat-num">{{ summary.total_events }}</div>
          <div class="stat-label">总事件</div>
        </div>
        <div class="stat-card stat-high">
          <div class="stat-num">{{ summary.high_count }}</div>
          <div class="stat-label">高优先级</div>
        </div>
        <div class="stat-card stat-medium">
          <div class="stat-num">{{ summary.medium_count }}</div>
          <div class="stat-label">中优先级</div>
        </div>
        <div class="stat-card stat-low">
          <div class="stat-num">{{ summary.low_count }}</div>
          <div class="stat-label">低优先级</div>
        </div>
      </div>

      <!-- Narrative -->
      <section class="block" v-if="summary.narrative">
        <h3 class="block-title">当日概览</h3>
        <p class="narrative">{{ summary.narrative }}</p>
      </section>

      <!-- Highlights -->
      <section class="block" v-if="summary.highlights && summary.highlights.length">
        <h3 class="block-title">关键事件 ({{ summary.highlights.length }})</h3>
        <ul class="highlights">
          <li
            v-for="(h, i) in summary.highlights"
            :key="i"
            :style="{ borderLeftColor: severityColors[h.severity] }"
          >
            <span class="hl-time">{{ h.time }}</span>
            <span class="hl-channel">{{ h.channel }}</span>
            <span class="hl-summary">{{ h.summary }}</span>
            <span class="hl-severity" :class="'sev-' + h.severity">
              {{ h.severity }}
            </span>
          </li>
        </ul>
      </section>

      <!-- 通道活跃度 -->
      <section class="block" v-if="channelStatsArr.length">
        <h3 class="block-title">通道活跃度</h3>
        <div class="channel-stats">
          <div v-for="row in channelStatsArr" :key="row.ch" class="bar-row">
            <span class="bar-label">{{ row.ch }}</span>
            <div class="bar-track">
              <div class="bar-fill" :style="{ width: row.pct + '%' }" />
            </div>
            <span class="bar-count">{{ row.count }}</span>
          </div>
        </div>
      </section>

      <!-- 没事件的友好提示 -->
      <div
        v-if="summary.total_events === 0 && !isLiveAggregate"
        class="empty-day"
      >
        当日没有任何事件记录，监控系统全天平静。
      </div>
    </div>
  </div>
</template>

<style scoped>
.summary-page {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.summary-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  border-bottom: 1px solid var(--border-color);
  flex-shrink: 0;
  gap: 16px;
  flex-wrap: wrap;
}
.filter-group { display: flex; gap: 8px; flex-wrap: wrap; }
.filter-input,
.filter-select {
  padding: 6px 10px;
  border-radius: 6px;
  border: 1px solid var(--border-color);
  background: var(--bg-tertiary);
  color: var(--text-primary);
  font-size: 13px;
}
.btn-refresh {
  padding: 6px 14px;
  border-radius: 6px;
  background: var(--accent);
  color: white;
  font-size: 13px;
}
.btn-refresh:disabled { opacity: 0.5; cursor: wait; }
.generated-info {
  font-size: 12px;
  color: var(--text-muted);
}

.summary-error {
  margin: 8px 16px;
  padding: 10px 14px;
  background: rgba(239,68,68,0.1);
  border: 1px solid var(--danger);
  border-radius: 6px;
  color: var(--danger);
  font-size: 13px;
}

.summary-content {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 18px;
}

.aggregate-banner {
  padding: 10px 14px;
  background: rgba(59,130,246,0.1);
  border: 1px solid var(--accent);
  border-radius: 6px;
  font-size: 13px;
  color: var(--text-secondary);
}
.aggregate-banner code {
  background: var(--bg-tertiary);
  padding: 1px 6px;
  border-radius: 4px;
  font-family: 'SF Mono', Consolas, monospace;
  font-size: 12px;
  color: var(--text-primary);
}

.stat-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}
.stat-card {
  background: var(--bg-secondary);
  border-radius: 8px;
  padding: 18px 16px;
  text-align: center;
  border-top: 3px solid var(--text-muted);
}
.stat-card.stat-high { border-top-color: var(--danger); }
.stat-card.stat-medium { border-top-color: var(--warning); }
.stat-card.stat-low { border-top-color: var(--accent); }
.stat-num {
  font-size: 32px;
  font-weight: 700;
  color: var(--text-primary);
  font-variant-numeric: tabular-nums;
}
.stat-label {
  font-size: 12px;
  color: var(--text-muted);
  margin-top: 4px;
}

.block {
  background: var(--bg-secondary);
  border-radius: 8px;
  padding: 16px;
}
.block-title {
  font-size: 13px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-muted);
  margin-bottom: 12px;
}
.narrative {
  font-size: 14px;
  color: var(--text-primary);
  line-height: 1.7;
  white-space: pre-wrap;
}

.highlights {
  list-style: none;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.highlights li {
  display: grid;
  grid-template-columns: 60px 80px 1fr auto;
  gap: 12px;
  align-items: center;
  padding: 10px 12px;
  background: var(--bg-tertiary);
  border-radius: 6px;
  border-left: 3px solid var(--text-muted);
  font-size: 13px;
}
.hl-time {
  font-variant-numeric: tabular-nums;
  color: var(--text-muted);
}
.hl-channel {
  font-weight: 600;
  color: var(--text-primary);
}
.hl-summary {
  color: var(--text-primary);
  line-height: 1.4;
}
.hl-severity {
  padding: 2px 8px;
  border-radius: 10px;
  font-size: 11px;
  text-transform: uppercase;
  font-weight: 600;
}
.sev-high { background: rgba(239,68,68,0.15); color: var(--danger); }
.sev-medium { background: rgba(245,158,11,0.15); color: var(--warning); }
.sev-low { background: rgba(59,130,246,0.15); color: var(--accent); }

.channel-stats {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.bar-row {
  display: grid;
  grid-template-columns: 60px 1fr 50px;
  align-items: center;
  gap: 10px;
  font-size: 13px;
}
.bar-label {
  color: var(--text-secondary);
  font-weight: 600;
}
.bar-track {
  background: var(--bg-tertiary);
  height: 14px;
  border-radius: 4px;
  overflow: hidden;
}
.bar-fill {
  background: var(--accent);
  height: 100%;
  transition: width 0.4s;
}
.bar-count {
  color: var(--text-muted);
  text-align: right;
  font-variant-numeric: tabular-nums;
}

.empty-day {
  padding: 60px;
  text-align: center;
  color: var(--text-muted);
}
</style>
