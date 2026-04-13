/**
 * 视频网格 Store
 *
 * 管理视频窗口的布局和每个格子绑定的视频流信息。
 *
 * 知识点 - 布局设计:
 * - 视频监控系统常见布局: 1x1, 2x2, 3x3, 1+5（1大5小）
 * - 每种布局对应不同的 CSS Grid 配置
 * - 每个 cell 可以绑定一个设备通道，或者为空（待拖入）
 */
import { defineStore } from 'pinia'

export const LAYOUTS = {
  '1x1': { cols: 1, rows: 1, cells: 1, label: '1 画面' },
  '2x2': { cols: 2, rows: 2, cells: 4, label: '4 画面' },
  '3x3': { cols: 3, rows: 3, cells: 9, label: '9 画面' },
  '4x4': { cols: 4, rows: 4, cells: 16, label: '16 画面' },
}

function createEmptyCells(count) {
  return Array.from({ length: count }, (_, i) => ({
    index: i,
    deviceId: null,
    channelId: null,
    streamPath: null,
    deviceIp: null,
    mediamtxPort: null,
    label: null,
  }))
}

export const useVideoGridStore = defineStore('videoGrid', {
  state: () => ({
    currentLayout: '2x2',
    cells: createEmptyCells(4),
    selectedCellIndex: null,
  }),

  getters: {
    layoutConfig: (state) => LAYOUTS[state.currentLayout],
    activeCells: (state) => state.cells.filter(c => c.streamPath !== null),
    selectedCell: (state) => {
      if (state.selectedCellIndex === null) return null
      return state.cells[state.selectedCellIndex]
    }
  },

  actions: {
    setLayout(layoutKey) {
      if (!LAYOUTS[layoutKey]) return
      const newCount = LAYOUTS[layoutKey].cells

      const oldCells = [...this.cells]
      this.cells = createEmptyCells(newCount)

      // 保留之前已绑定的通道（在新布局容量范围内）
      for (let i = 0; i < Math.min(oldCells.length, newCount); i++) {
        if (oldCells[i].streamPath) {
          this.cells[i] = { ...oldCells[i], index: i }
        }
      }

      this.currentLayout = layoutKey
      if (this.selectedCellIndex !== null && this.selectedCellIndex >= newCount) {
        this.selectedCellIndex = null
      }
    },

    /**
     * 将设备通道绑定到指定的 cell
     * 这是拖拽操作的核心方法
     */
    assignChannel(cellIndex, { deviceId, channelId, streamPath, deviceIp, mediamtxPort, label }) {
      if (cellIndex < 0 || cellIndex >= this.cells.length) return
      this.cells[cellIndex] = {
        index: cellIndex,
        deviceId,
        channelId,
        streamPath,
        deviceIp,
        mediamtxPort,
        label,
      }
    },

    clearCell(cellIndex) {
      if (cellIndex < 0 || cellIndex >= this.cells.length) return
      this.cells[cellIndex] = {
        index: cellIndex,
        deviceId: null,
        channelId: null,
        streamPath: null,
        deviceIp: null,
        mediamtxPort: null,
        label: null,
      }
    },

    clearAll() {
      this.cells = createEmptyCells(this.cells.length)
      this.selectedCellIndex = null
    },

    selectCell(index) {
      this.selectedCellIndex = index
    }
  }
})
