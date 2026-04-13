/**
 * 设备管理 Store (Pinia)
 *
 * 知识点 - Pinia Store 结构:
 * - defineStore('id', { ... }) 第一个参数是全局唯一 ID
 * - state: 返回一个函数，函数返回初始状态对象（类似 Vue 2 的 data()）
 * - getters: 类似 computed，根据 state 派生出的计算值，自动缓存
 * - actions: 可以是同步或异步方法，用于修改 state
 *
 * 知识点 - 响应式原理:
 * - Pinia 内部使用 Vue 的 reactive() 包装 state
 * - 当 state 变化时，所有使用该 state 的组件自动更新（依赖追踪）
 */
import { defineStore } from 'pinia'
import { mockDevices } from '@/mock/devices'

export const useDeviceStore = defineStore('devices', {
  state: () => ({
    devices: [],
    loading: false,
    error: null
  }),

  getters: {
    onlineDevices: (state) => state.devices.filter(d => d.status === 'online'),
    offlineDevices: (state) => state.devices.filter(d => d.status === 'offline'),
    totalChannels: (state) => state.devices.reduce((sum, d) => sum + d.channels.length, 0),

    getDeviceById: (state) => (id) => state.devices.find(d => d.id === id),

    getChannel: (state) => (deviceId, channelId) => {
      const device = state.devices.find(d => d.id === deviceId)
      if (!device) return null
      return device.channels.find(ch => ch.id === channelId)
    }
  },

  actions: {
    async fetchDevices() {
      this.loading = true
      this.error = null
      try {
        // TODO: 后续替换为真实 API 请求
        // const res = await fetch('/api/devices')
        // this.devices = await res.json()
        await new Promise(resolve => setTimeout(resolve, 300))
        this.devices = mockDevices
      } catch (err) {
        this.error = err.message
      } finally {
        this.loading = false
      }
    },

    addDevice(device) {
      this.devices.push(device)
    },

    removeDevice(deviceId) {
      this.devices = this.devices.filter(d => d.id !== deviceId)
    },

    updateDeviceStatus(deviceId, status) {
      const device = this.devices.find(d => d.id === deviceId)
      if (device) device.status = status
    }
  }
})
