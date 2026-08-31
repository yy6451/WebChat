import type { WsMessage } from '../types/chat'

export class ChatSocket {
  private ws: WebSocket | null = null
  private reconnectTimer: number | null = null
  private reconnectAttempts = 0

  constructor(
    private readonly url: string,
    private readonly onMessage: (msg: WsMessage) => void,
    private readonly onState: (state: 'open' | 'close' | 'error' | 'connecting') => void
  ) {}

  connect() {
    this.onState('connecting')
    this.ws = new WebSocket(this.url)
    this.ws.onopen = () => {
      this.reconnectAttempts = 0
      this.onState('open')
    }
    this.ws.onmessage = (ev) => {
      try {
        this.onMessage(JSON.parse(ev.data) as WsMessage)
      } catch {
        // ignore invalid payload
      }
    }
    this.ws.onclose = () => {
      this.onState('close')
      this.reconnect()
    }
    this.ws.onerror = () => {
      this.onState('error')
    }
  }

  send(msg: WsMessage) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return
    this.ws.send(JSON.stringify(msg))
  }

  close() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
    this.ws?.close()
    this.ws = null
  }

  private reconnect() {
    if (this.reconnectTimer) return
    // 指数退避: 1s → 2s → 4s → 8s → 16s → 最大 30s
    const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts), 30000)
    this.reconnectAttempts++
    this.reconnectTimer = window.setTimeout(() => {
      this.reconnectTimer = null
      this.connect()
    }, delay)
  }
}