<!--
  顶部导航栏

  知识点 - <router-link>:
  Vue Router 提供的声明式导航组件，渲染为 <a> 标签但不会刷新页面。
  - to="/video" 指定跳转路径
  - 当路由匹配时，自动添加 .router-link-active 类（用于高亮当前页）

  知识点 - $route:
  在模板中可以通过 $route 访问当前路由信息：
  - $route.path: 当前路径（如 "/video"）
  - $route.name: 路由名称
  - $route.meta: 路由元信息
-->
<script setup>
import { useRoute } from 'vue-router'
import { computed } from 'vue'

const route = useRoute()

const navItems = [
  { path: '/video', label: '实时视频', icon: '📹' },
  { path: '/events', label: '事件列表', icon: '🔔' },
  { path: '/summary', label: '日报总结', icon: '📊' },
  { path: '/devices', label: '设备管理', icon: '🖥️' },
]

const currentTime = computed(() => {
  return new Date().toLocaleString('zh-CN')
})

// 每秒更新时钟
import { ref, onMounted, onUnmounted } from 'vue'
const clock = ref(new Date().toLocaleString('zh-CN'))
let clockTimer = null
onMounted(() => {
  clockTimer = setInterval(() => {
    clock.value = new Date().toLocaleString('zh-CN')
  }, 1000)
})
onUnmounted(() => {
  clearInterval(clockTimer)
})
</script>

<template>
  <header class="app-header">
    <div class="header-left">
      <div class="logo">
        <svg width="24" height="24" viewBox="0 0 64 64" fill="none">
          <rect width="64" height="64" rx="12" fill="#3b82f6"/>
          <circle cx="32" cy="28" r="10" stroke="white" stroke-width="3" fill="none"/>
          <circle cx="32" cy="28" r="3" fill="white"/>
          <rect x="20" y="44" width="24" height="3" rx="1.5" fill="white" opacity="0.7"/>
        </svg>
      </div>
      <span class="app-title">Video Management System</span>
    </div>

    <nav class="header-nav">
      <router-link
        v-for="item in navItems"
        :key="item.path"
        :to="item.path"
        class="nav-item"
        :class="{ active: route.path === item.path }"
      >
        <span class="nav-icon">{{ item.icon }}</span>
        <span class="nav-label">{{ item.label }}</span>
      </router-link>
    </nav>

    <div class="header-right">
      <span class="clock">{{ clock }}</span>
    </div>
  </header>
</template>

<style scoped>
.app-header {
  height: var(--header-height);
  background: var(--bg-secondary);
  border-bottom: 1px solid var(--border-color);
  display: flex;
  align-items: center;
  padding: 0 16px;
  gap: 24px;
  flex-shrink: 0;
}
.header-left {
  display: flex;
  align-items: center;
  gap: 10px;
}
.app-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--text-primary);
  white-space: nowrap;
}
.header-nav {
  display: flex;
  gap: 2px;
  flex: 1;
  justify-content: center;
}
.nav-item {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 16px;
  border-radius: 6px;
  color: var(--text-secondary);
  font-size: 13px;
  transition: all 0.2s;
  text-decoration: none;
}
.nav-item:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}
.nav-item.active {
  background: var(--accent);
  color: white;
}
.nav-icon {
  font-size: 14px;
}
.header-right {
  display: flex;
  align-items: center;
  gap: 12px;
}
.clock {
  font-size: 12px;
  color: var(--text-muted);
  white-space: nowrap;
  font-variant-numeric: tabular-nums;
}
</style>
