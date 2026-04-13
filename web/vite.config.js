import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { fileURLToPath, URL } from 'node:url'

/**
 * Vite 配置
 *
 * 关键概念:
 * - resolve.alias: 路径别名，让 import 时可以用 @/ 代替 ./src/
 * - server.proxy: 开发时的 API 代理，解决跨域问题（CORS）
 *   浏览器会把 /api/mediamtx 的请求转发到 MediaMTX 服务器
 */
export default defineConfig({
  plugins: [vue()],

  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url))
    }
  },

  server: {
    host: '0.0.0.0',
    port: 3000,
    proxy: {
      '/api/mediamtx': {
        target: 'http://192.168.3.34:9997',
        changeOrigin: true,
        rewrite: (path) => path.replace(/^\/api\/mediamtx/, '')
      }
    }
  }
})
