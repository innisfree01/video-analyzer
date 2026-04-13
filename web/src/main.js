/**
 * Vue 应用入口
 *
 * 知识点 - Vue 应用启动流程:
 * 1. createApp(App) 创建 Vue 应用实例，App 是根组件
 * 2. app.use(plugin) 安装插件（Router、Pinia 等）
 *    - 插件机制是 Vue 的扩展方式，插件可以添加全局功能
 * 3. app.mount('#app') 将 Vue 接管 HTML 中 id="app" 的 DOM 元素
 *    - 之后整个页面的渲染由 Vue 控制（SPA 单页应用）
 *
 * 知识点 - 插件加载顺序:
 * - Pinia 必须在 Router 之前注册（因为路由守卫中可能用到 Store）
 */
import { createApp } from 'vue'
import { createPinia } from 'pinia'
import router from './router'
import App from './App.vue'
import './style.css'

const app = createApp(App)

app.use(createPinia())
app.use(router)

app.mount('#app')
