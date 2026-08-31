<script setup lang="ts">
defineProps<{ messages: Array<{ role: 'self' | 'other' | 'system'; meta: string; content: string }> }>()
</script>

<template>
  <div class="messages">
    <div v-for="(m, idx) in messages" :key="idx" class="msg" :class="m.role">
      <div class="meta">{{ m.meta }}</div>
      <div class="body">{{ m.content }}</div>
    </div>
  </div>
</template>

<style scoped>
.messages { flex: 1; min-height: 0; overflow-y: auto; overflow-x: hidden; padding: 16px 18px; display: flex; flex-direction: column; gap: 10px; background: #fff; }
.msg { display: flex; flex-direction: column; max-width: 72%; }
.msg.self { align-self: flex-end; text-align: right; }
.msg.other { align-self: flex-start; text-align: left; }
.msg.system { align-self: center; text-align: center; max-width: 90%; }
.meta { font-size: 11px; color: var(--muted); letter-spacing: 0.02em; }
.body { display: inline-block; margin-top: 4px; padding: 10px 12px; border-radius: 16px; background: #f1f5ff; box-shadow: 0 10px 20px rgba(15, 91, 255, 0.08); }
.msg.self .body { background: linear-gradient(135deg, #0f5bff, #5f84ff); color: #fff; }
.msg.system .body { background: #fff4e6; color: #9a3412; }
</style>