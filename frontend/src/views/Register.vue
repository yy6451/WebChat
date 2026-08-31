<script setup lang="ts">
import { useRouter } from 'vue-router'
import RegisterForm from '../components/RegisterForm.vue'
import { register } from '../api/http'

const router = useRouter()

async function onSubmit(username: string, password: string, repassword: string) {
  const resp = await register(username, password, repassword)
  if (resp.code !== 0) {
    alert(resp.msg || '注册失败')
    return
  }
  alert('注册成功，请登录')
  router.push('/login')
}
</script>

<template>
  <main class="auth-page">
    <div class="auth-bg">
      <div class="orb orb-a"></div>
      <div class="orb orb-b"></div>
      <div class="grid"></div>
    </div>
    <section class="auth-card">
      <header class="brand">
        <div class="logo">PulseChat</div>
        <p class="tagline">创建账号，开启好友私聊与群聊</p>
      </header>
      <RegisterForm @submit="onSubmit" />
      <p class="tip">已有账号？<router-link to="/login">去登录</router-link></p>
    </section>
  </main>
</template>

<style scoped>
.auth-page { min-height: 100vh; display: grid; place-items: center; position: relative; overflow: hidden; padding: 24px; }
.auth-bg { position: absolute; inset: 0; z-index: 0; pointer-events: none; }
.orb { position: absolute; opacity: 0.8; border-radius: 50%; }
.orb-a { width: 360px; height: 360px; background: radial-gradient(circle, #ffd9c4 0%, #ff6b4a 58%, transparent 70%); top: -120px; left: -80px; }
.orb-b { width: 420px; height: 420px; background: radial-gradient(circle, #b9e3ff 0%, #0f5bff 55%, transparent 70%); bottom: -160px; right: -120px; }
.grid { position: absolute; inset: 0; background-image: linear-gradient(rgba(14, 15, 18, 0.04) 1px, transparent 1px), linear-gradient(90deg, rgba(14, 15, 18, 0.04) 1px, transparent 1px); background-size: 36px 36px; opacity: 0.35; }
.auth-card { width: min(420px, 92vw); background: var(--card); border-radius: var(--radius-lg); padding: 28px; box-shadow: var(--shadow); position: relative; z-index: 1; animation: floatIn 0.4s ease; border: 1px solid var(--stroke); }
.brand { margin-bottom: 16px; }
.logo { font-size: 28px; font-weight: 700; letter-spacing: 0.4px; }
.tagline { margin: 6px 0 0; color: var(--muted); font-size: 14px; }
.tip { color: var(--muted); margin-top: 12px; text-align: center; }
@media (max-width: 520px) {
  .auth-card { padding: 20px; }
}
</style>