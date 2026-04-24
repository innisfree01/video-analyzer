/**
 * Vue Router 配置
 *
 * 核心概念:
 * - 路由 (Route): URL 路径与组件的映射关系
 * - 路由视图 (<router-view>): 在 App.vue 中放置，路由匹配时自动渲染对应组件
 * - 路由链接 (<router-link>): 声明式导航，点击后切换路由
 * - 懒加载 (Lazy Loading): 使用 () => import() 语法，组件只在访问时才加载
 *   这样首屏只下载当前页面的代码，减小初始包体积
 */
import { createRouter, createWebHistory } from 'vue-router'

const router = createRouter({
  history: createWebHistory(),

  routes: [
    {
      path: '/',
      redirect: '/video'
    },
    {
      path: '/video',
      name: 'video',
      component: () => import('@/views/VideoView.vue'),
      meta: { title: '实时视频', icon: 'videocam' }
    },
    {
      path: '/events',
      name: 'events',
      component: () => import('@/views/EventsView.vue'),
      meta: { title: '事件列表', icon: 'notifications' }
    },
    {
      path: '/summary',
      name: 'summary',
      component: () => import('@/views/SummaryView.vue'),
      meta: { title: '日报总结', icon: 'summarize' }
    },
    {
      path: '/devices',
      name: 'devices',
      component: () => import('@/views/DevicesView.vue'),
      meta: { title: '设备管理', icon: 'devices' }
    }
  ]
})

router.beforeEach((to) => {
  document.title = `${to.meta.title || 'VMS'} - Video Management System`
})

export default router
