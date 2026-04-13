<!--
  布局选择器

  知识点 - v-for 和 :key:
  v-for 用于列表渲染，:key 是必须提供的唯一标识。
  Vue 内部使用 key 来高效更新 DOM（虚拟 DOM diff 算法）。
  没有 key 或 key 不唯一会导致渲染 bug。
-->
<script setup>
import { LAYOUTS } from '@/stores/videoGrid'

defineProps({
  current: String
})
const emit = defineEmits(['change'])
</script>

<template>
  <div class="layout-selector">
    <button
      v-for="(config, key) in LAYOUTS"
      :key="key"
      class="layout-btn"
      :class="{ active: current === key }"
      :title="config.label"
      @click="emit('change', key)"
    >
      <div class="layout-grid" :style="{
        gridTemplateColumns: `repeat(${config.cols}, 1fr)`,
        gridTemplateRows: `repeat(${config.rows}, 1fr)`,
      }">
        <div
          v-for="i in config.cells"
          :key="i"
          class="layout-cell"
        ></div>
      </div>
      <span class="layout-label">{{ config.label }}</span>
    </button>
  </div>
</template>

<style scoped>
.layout-selector {
  display: flex;
  gap: 6px;
}
.layout-btn {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  padding: 6px 10px;
  border-radius: 6px;
  transition: all 0.15s;
}
.layout-btn:hover {
  background: var(--bg-tertiary);
}
.layout-btn.active {
  background: var(--accent);
}
.layout-grid {
  display: grid;
  gap: 1px;
  width: 24px;
  height: 24px;
}
.layout-cell {
  background: var(--text-muted);
  border-radius: 1px;
  opacity: 0.5;
}
.layout-btn.active .layout-cell {
  background: white;
  opacity: 0.8;
}
.layout-label {
  font-size: 10px;
  color: var(--text-muted);
  white-space: nowrap;
}
.layout-btn.active .layout-label {
  color: white;
}
</style>
