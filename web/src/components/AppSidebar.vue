<!--
  左侧边栏

  知识点 - 组件通信:
  Vue 组件之间传递数据有几种方式：
  1. Props (父→子): 父组件通过属性传值给子组件
  2. Emit (子→父): 子组件触发事件通知父组件
  3. Pinia Store (任意组件): 通过全局状态管理共享数据
  4. Provide/Inject (祖先→后代): 跨层级传值

  这里侧边栏和视频网格通过 Pinia Store 通信：
  侧边栏拖拽设备 → Pinia videoGridStore → 视频网格更新
-->
<script setup>
import DeviceTree from '@/components/DeviceTree.vue'
import { useRoute } from 'vue-router'

const route = useRoute()
</script>

<template>
  <aside class="app-sidebar">
    <div class="sidebar-section">
      <div class="section-header">
        <span class="section-title">设备列表</span>
      </div>
      <div class="section-body">
        <DeviceTree />
      </div>
    </div>

    <div class="sidebar-footer">
      <div class="footer-info">
        <span class="dot online"></span>
        <span>系统运行中</span>
      </div>
    </div>
  </aside>
</template>

<style scoped>
.app-sidebar {
  width: var(--sidebar-width);
  background: var(--bg-secondary);
  border-right: 1px solid var(--border-color);
  display: flex;
  flex-direction: column;
  flex-shrink: 0;
  overflow: hidden;
}
.sidebar-section {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}
.section-header {
  padding: 12px 16px;
  border-bottom: 1px solid var(--border-color);
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.section-title {
  font-size: 12px;
  font-weight: 600;
  text-transform: uppercase;
  color: var(--text-muted);
  letter-spacing: 0.5px;
}
.section-body {
  flex: 1;
  overflow-y: auto;
  padding: 8px;
}
.sidebar-footer {
  padding: 10px 16px;
  border-top: 1px solid var(--border-color);
}
.footer-info {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
  color: var(--text-muted);
}
.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}
.dot.online {
  background: var(--success);
  box-shadow: 0 0 6px var(--success);
}
</style>
