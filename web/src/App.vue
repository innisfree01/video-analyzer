<!--
  根组件 - 定义整体页面布局

  知识点 - 布局结构:
  ┌─────────────────────────────────────┐
  │              AppHeader              │  ← 顶部导航栏 (48px)
  ├──────────┬──────────────────────────┤
  │          │                          │
  │ Sidebar  │    <router-view />       │  ← 路由视图（页面内容）
  │  280px   │                          │
  │          │                          │
  └──────────┴──────────────────────────┘

  知识点 - <router-view>:
  Vue Router 的核心组件，它会根据当前 URL 自动渲染对应的页面组件。
  比如访问 /video 时渲染 VideoView，访问 /events 时渲染 EventsView。
-->
<script setup>
import AppHeader from '@/components/AppHeader.vue'
import AppSidebar from '@/components/AppSidebar.vue'
import { useDeviceStore } from '@/stores/devices'
import { onMounted } from 'vue'

const deviceStore = useDeviceStore()

onMounted(() => {
  deviceStore.fetchDevices()
})
</script>

<template>
  <div class="app-layout">
    <AppHeader />
    <div class="app-body">
      <AppSidebar />
      <main class="app-main">
        <router-view />
      </main>
    </div>
  </div>
</template>

<style scoped>
.app-layout {
  display: flex;
  flex-direction: column;
  height: 100vh;
}
.app-body {
  display: flex;
  flex: 1;
  overflow: hidden;
}
.app-main {
  flex: 1;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}
</style>
