/**
 * WebRTC WHEP 播放器 Composable
 *
 * 知识点 - WebRTC (Web Real-Time Communication):
 * ============================================
 * WebRTC 是浏览器内置的实时通信技术，可以实现低延迟的音视频传输。
 *
 * 核心流程 (WHEP - WebRTC-HTTP Egress Protocol):
 * 1. 浏览器创建 RTCPeerConnection（WebRTC 连接对象）
 * 2. 调用 createOffer() 生成 SDP Offer（会话描述，包含支持的编解码器等信息）
 * 3. 将 SDP Offer 通过 HTTP POST 发送到 MediaMTX 的 WHEP 端点
 * 4. MediaMTX 返回 SDP Answer（服务端的会话描述）
 * 5. 双方通过 ICE (Interactive Connectivity Establishment) 协商网络路径
 * 6. 建立 P2P 连接后，视频流直接传输到浏览器
 *
 * 关键术语:
 * - SDP (Session Description Protocol): 描述媒体会话的文本协议
 * - ICE: 用于穿越 NAT/防火墙的网络连接协商机制
 * - STUN: 帮助发现公网 IP 的服务器
 * - WHEP: 标准化的 WebRTC 拉流协议（对应 WHIP 是推流协议）
 *
 * 知识点 - Vue Composable:
 * =======================
 * - ref() 创建响应式变量，.value 访问/修改值
 * - watch() 监听响应式变量变化，自动执行回调
 * - onUnmounted() 组件销毁时的清理钩子，防止内存泄漏
 * - 返回的对象中的 ref 在模板中自动解包（不需要 .value）
 */
import { ref, watch, onUnmounted } from 'vue'

const RECONNECT_DELAY = 3000
/*
 * In a LAN-only deployment, ICE candidates discovered locally are sufficient.
 * Public STUN servers (e.g. stun.l.google.com) require outbound internet
 * access and may delay or even fail ICE gathering on isolated networks.
 * Leave empty for LAN; add { urls: 'stun:...' } when going across networks.
 */
const ICE_SERVERS = []

export function useWhepPlayer() {
  const videoRef = ref(null)
  const status = ref('idle')    // idle | connecting | live | error
  const errorMsg = ref('')

  let pc = null
  let sessionUrl = null
  let reconnectTimer = null
  let currentWhepUrl = null
  let running = false

  function buildWhepUrl(deviceIp, mediamtxPort, streamPath) {
    return `http://${deviceIp}:${mediamtxPort}/${streamPath}/whep`
  }

  async function connect(whepUrl) {
    if (!running) return
    status.value = 'connecting'
    errorMsg.value = ''

    try {
      cleanup()

      pc = new RTCPeerConnection({ iceServers: ICE_SERVERS })

      // addTransceiver: 告诉 WebRTC 我们只接收视频（recvonly）
      pc.addTransceiver('video', { direction: 'recvonly' })

      // ontrack: 当远端媒体流到达时触发
      pc.ontrack = (event) => {
        if (videoRef.value) {
          videoRef.value.srcObject = event.streams[0]
          status.value = 'live'
        }
      }

      // 监听 ICE 连接状态变化
      pc.oniceconnectionstatechange = () => {
        const state = pc?.iceConnectionState
        if (state === 'disconnected' || state === 'failed' || state === 'closed') {
          status.value = 'error'
          errorMsg.value = `连接断开 (${state})`
          scheduleReconnect()
        }
      }

      // Step 1: 创建 SDP Offer
      const offer = await pc.createOffer()
      await pc.setLocalDescription(offer)

      // Step 2: POST Offer 到 WHEP 端点
      const response = await fetch(whepUrl, {
        method: 'POST',
        headers: { 'Content-Type': 'application/sdp' },
        body: pc.localDescription.sdp
      })

      if (response.status !== 201) {
        throw new Error(`WHEP 请求失败: HTTP ${response.status}`)
      }

      // Step 3: 保存 session URL（用于后续关闭连接）
      sessionUrl = new URL(response.headers.get('Location'), whepUrl).toString()

      // Step 4: 设置 SDP Answer
      const answerSDP = await response.text()
      await pc.setRemoteDescription({ type: 'answer', sdp: answerSDP })

    } catch (err) {
      console.warn(`[WHEP] 连接失败:`, err.message)
      status.value = 'error'
      errorMsg.value = err.message
      scheduleReconnect()
    }
  }

  function scheduleReconnect() {
    if (!running) return
    if (reconnectTimer) clearTimeout(reconnectTimer)
    reconnectTimer = setTimeout(() => {
      if (currentWhepUrl && running) connect(currentWhepUrl)
    }, RECONNECT_DELAY)
  }

  function cleanup() {
    if (reconnectTimer) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
    if (pc) {
      pc.close()
      pc = null
    }
    if (sessionUrl) {
      fetch(sessionUrl, { method: 'DELETE' }).catch(() => {})
      sessionUrl = null
    }
  }

  /**
   * 开始播放指定流
   * @param {string} deviceIp - 设备 IP
   * @param {number} mediamtxPort - MediaMTX WHEP 端口
   * @param {string} streamPath - 流路径 (如 'cam0')
   */
  function play(deviceIp, mediamtxPort, streamPath) {
    stop()
    running = true
    currentWhepUrl = buildWhepUrl(deviceIp, mediamtxPort, streamPath)
    connect(currentWhepUrl)
  }

  function stop() {
    running = false
    cleanup()
    status.value = 'idle'
    errorMsg.value = ''
    currentWhepUrl = null
    if (videoRef.value) {
      videoRef.value.srcObject = null
    }
  }

  onUnmounted(() => {
    stop()
  })

  return {
    videoRef,
    status,
    errorMsg,
    play,
    stop,
  }
}
