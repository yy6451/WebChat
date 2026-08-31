<script setup lang="ts">
import { computed, ref } from 'vue'
import type { WsMessage } from '../types/chat'
import MessageList from './MessageList.vue'

const props = defineProps<{ target: string; username: string; messages: Array<{ role: 'self' | 'other' | 'system'; meta: string; content: string }> }>()
const emit = defineEmits<{ (e: 'send', msg: WsMessage): void }>()

const input = ref('')
const title = computed(() => (props.target === 'room_default' ? '公共聊天室' : `私聊：${props.target}`))

function send() {
  const content = input.value.trim()
  if (!content) return
  emit('send', {
    type: props.target === 'room_default' ? 'chat' : 'private',
    from: props.username,
    to: props.target,
    content,
    timestamp: Math.floor(Date.now() / 1000)
  })
  input.value = ''
}
</script>

<template>
  <section class="chat-room">
    <header class="header">{{ title }}</header>
    <MessageList :messages="messages" />
    <footer class="composer">
      <input v-model="input" placeholder="输入消息，回车发送" @keydown.enter.prevent="send" />
      <button @click="send">发送</button>
    </footer>
  </section>
</template>

<style scoped>
.chat-room { flex: 1; display: flex; flex-direction: column; min-height: 0; overflow: hidden; background: #fff; }
.header { padding: 14px 18px; border-bottom: 1px solid var(--stroke); font-weight: 700; font-size: 14px; background: #f9fbff; }
.composer { display: flex; gap: 8px; padding: 12px 16px; border-top: 1px solid var(--stroke); flex-shrink: 0; background: #fff; }
.composer input { flex: 1; padding: 12px 14px; border: 1px solid var(--stroke); border-radius: var(--radius-md); background: #f9fafb; }
.composer input:focus { outline: 2px solid rgba(15, 91, 255, 0.2); border-color: var(--brand); background: #fff; }
.composer button { border: 0; background: linear-gradient(135deg, var(--brand), #4f7dff); color: #fff; border-radius: var(--radius-md); padding: 0 18px; font-weight: 600; cursor: pointer; }
</style>