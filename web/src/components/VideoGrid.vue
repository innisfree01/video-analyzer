<!--
  视频网格容器

  知识点 - CSS Grid 动态布局:
  ================================
  CSS Grid 是二维布局系统，非常适合视频监控的多画面排列。
  
  核心属性:
  - grid-template-columns: repeat(N, 1fr) → N 列等宽
  - grid-template-rows: repeat(M, 1fr) → M 行等高
  - gap: 格子之间的间距
  - 1fr 是 "fraction" 单位，表示剩余空间的等分

  通过 Pinia Store 中的布局配置动态改变网格：
  - 2x2 → repeat(2, 1fr)  → 4 个等大格子
  - 3x3 → repeat(3, 1fr)  → 9 个等大格子

  知识点 - Vue 动态样式绑定:
  :style="{ ... }" 可以用 JS 对象动态设置 CSS 属性。
  当 Store 中的 layout 变化时，grid 自动重排。
-->
<script setup>
import { useVideoGridStore } from '@/stores/videoGrid'
import VideoCell from '@/components/VideoCell.vue'
import LayoutSelector from '@/components/LayoutSelector.vue'

const gridStore = useVideoGridStore()
</script>

<template>
  <div class="video-grid-container">
    <!-- 工具栏 -->
    <div class="grid-toolbar">
      <LayoutSelector
        :current="gridStore.currentLayout"
        @change="gridStore.setLayout"
      />
      <div class="toolbar-actions">
        <span class="active-count">
          {{ gridStore.activeCells.length }} / {{ gridStore.cells.length }} 画面
        </span>
        <button
          class="btn-clear"
          :disabled="gridStore.activeCells.length === 0"
          @click="gridStore.clearAll()"
        >
          全部清除
        </button>
      </div>
    </div>

    <!-- 视频网格 -->
    <div
      class="video-grid"
      :style="{
        gridTemplateColumns: `repeat(${gridStore.layoutConfig.cols}, 1fr)`,
        gridTemplateRows: `repeat(${gridStore.layoutConfig.rows}, 1fr)`,
      }"
    >
      <VideoCell
        v-for="cell in gridStore.cells"
        :key="cell.index"
        :cell-index="cell.index"
        :cell="cell"
      />
    </div>
  </div>
</template>

<style scoped>
.video-grid-container {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}
.grid-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 12px;
  border-bottom: 1px solid var(--border-color);
  flex-shrink: 0;
}
.toolbar-actions {
  display: flex;
  align-items: center;
  gap: 12px;
}
.active-count {
  font-size: 12px;
  color: var(--text-muted);
}
.btn-clear {
  padding: 4px 12px;
  border-radius: 4px;
  font-size: 12px;
  color: var(--text-secondary);
  background: var(--bg-tertiary);
  transition: all 0.15s;
}
.btn-clear:hover:not(:disabled) {
  background: var(--danger);
  color: white;
}
.btn-clear:disabled {
  opacity: 0.3;
  cursor: not-allowed;
}
.video-grid {
  flex: 1;
  display: grid;
  gap: 4px;
  padding: 4px;
  background: var(--bg-primary);
}
</style>
