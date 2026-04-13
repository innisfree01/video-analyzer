<!--
  实时视频页面

  知识点 - Vue 组件组合:
  ================================
  这个页面本身很简洁，因为逻辑都封装在子组件中了。
  这体现了 Vue 的组件化思想：
  - 每个组件只负责一个职责（单一职责原则）
  - VideoGrid 负责布局管理和格子渲染
  - VideoCell 负责单个播放窗口
  - DeviceTree（在侧边栏中）负责设备展示和拖拽

  页面组件（View）的角色是"编排"这些子组件。
-->
<script setup>
import VideoGrid from '@/components/VideoGrid.vue'
</script>

<template>
  <VideoGrid />
</template>
