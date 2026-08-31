<script setup lang="ts">
import { ref } from 'vue'

const emit = defineEmits<{ (e: 'submit', username: string, password: string, repassword: string): void }>()
const username = ref('')
const password = ref('')
const repassword = ref('')
</script>

<template>
  <form class="card" @submit.prevent="emit('submit', username, password, repassword)">
    <h2>创建账号</h2>
    <p class="hint">几秒即可加入聊天室</p>
    <label class="field">
      <span>用户名</span>
      <input v-model="username" placeholder="设定用户名" required />
    </label>
    <label class="field">
      <span>密码</span>
      <input v-model="password" placeholder="设定密码" required type="password" />
    </label>
    <label class="field">
      <span>确认密码</span>
      <input v-model="repassword" placeholder="再次输入密码" required type="password" />
    </label>
    <button type="submit">完成注册</button>
  </form>
</template>

<style scoped>
.card { display: flex; flex-direction: column; gap: 12px; }
h2 { margin: 0; font-size: 22px; }
.hint { margin: -4px 0 6px; color: var(--muted); font-size: 13px; }
.field { display: flex; flex-direction: column; gap: 6px; font-size: 12px; color: var(--muted); }
.field span { text-transform: uppercase; letter-spacing: 0.08em; }
input { padding: 12px 14px; border: 1px solid var(--stroke); border-radius: var(--radius-md); background: #f9fafb; color: var(--ink); }
input:focus { outline: 2px solid rgba(15, 91, 255, 0.2); border-color: var(--brand); background: #fff; }
button { padding: 12px; border: 0; border-radius: var(--radius-md); background: linear-gradient(135deg, var(--brand), #4f7dff); color: #fff; font-weight: 600; cursor: pointer; box-shadow: 0 12px 24px rgba(15, 91, 255, 0.2); }
button:hover { transform: translateY(-1px); }
</style>