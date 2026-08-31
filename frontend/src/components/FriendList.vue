<script setup lang="ts">
import { ref } from 'vue'
import type { FriendItem } from '../types/chat'

const props = defineProps<{
  users: FriendItem[]
  current: string
  me: string
  pendingRequests?: string[]
  searching?: boolean
  searchMessage?: string
  searchResult?: { username: string; exists: boolean; isFriend: boolean } | null
}>()
const emit = defineEmits<{
  (e: 'select', user: string): void
  (e: 'search', username: string): void
  (e: 'add', username: string): void
  (e: 'verify', payload: { friend: string; action: 'accept' | 'reject' }): void
}>()

const keyword = ref('')

function doSearch() {
  const v = keyword.value.trim()
  if (!v) return
  emit('search', v)
}

function doAdd() {
  const v = (props.searchResult?.username || keyword.value).trim()
  if (!v) return
  emit('add', v)
}
</script>

<template>
  <aside class="friend-list">
    <button class="item" :class="{ active: current === 'room_default' }" @click="emit('select', 'room_default')">
      公共聊天室
    </button>
    <div class="title">好友列表</div>

    <div class="add-panel">
      <input v-model="keyword" class="search-input" placeholder="搜索用户名后添加好友" @keydown.enter.prevent="doSearch" />
      <button class="search-btn" :disabled="!!searching" @click="doSearch">{{ searching ? '搜索中...' : '搜索' }}</button>
      <div v-if="searchMessage" class="search-msg">{{ searchMessage }}</div>
      <div v-if="searchResult" class="search-result">
        <span>{{ searchResult.username }}</span>
        <button v-if="searchResult.exists && !searchResult.isFriend" class="add-btn" @click="doAdd">添加</button>
        <span v-else-if="searchResult.isFriend" class="hint">已是好友</span>
        <span v-else class="hint">用户不存在</span>
      </div>
    </div>

    <div v-if="(pendingRequests || []).length > 0" class="title">待验证请求</div>
    <div v-for="u in (pendingRequests || [])" :key="`req_${u}`" class="request-item">
      <span class="req-name">{{ u }}</span>
      <div class="req-actions">
        <button class="ok-btn" @click="emit('verify', { friend: u, action: 'accept' })">通过</button>
        <button class="no-btn" @click="emit('verify', { friend: u, action: 'reject' })">拒绝</button>
      </div>
    </div>

    <button
      v-for="u in props.users.filter((u) => u.username !== props.me)"
      :key="u.username"
      class="item"
      :class="{ active: current === u.username }"
      @click="emit('select', u.username)"
    >
      <span class="left">
        <span class="dot" :class="{ online: !!u.online }"></span>
        <span>{{ u.username }}</span>
      </span>
      <span v-if="u.unread > 0" class="badge">{{ u.unread }}</span>
    </button>
  </aside>
</template>

<style scoped>
.friend-list { width: 300px; padding: 16px; border-right: 1px solid var(--stroke); display: flex; flex-direction: column; overflow: hidden; background: #fbfcff; }
.title { margin: 10px 0; color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: 0.08em; }
.add-panel { padding: 10px; border: 1px solid var(--stroke); border-radius: var(--radius-md); margin-bottom: 12px; background: #fff; box-shadow: 0 12px 24px rgba(15, 91, 255, 0.06); }
.search-input { width: 100%; box-sizing: border-box; border: 1px solid var(--stroke); border-radius: var(--radius-md); padding: 10px 12px; margin-bottom: 8px; background: #f9fafb; }
.search-input:focus { outline: 2px solid rgba(15, 91, 255, 0.2); border-color: var(--brand); background: #fff; }
.search-btn, .add-btn { border: 0; border-radius: 999px; padding: 6px 12px; background: var(--brand); color: #fff; cursor: pointer; }
.search-btn:disabled { opacity: 0.6; cursor: not-allowed; }
.search-msg { margin-top: 8px; font-size: 12px; color: var(--muted); }
.search-result { margin-top: 8px; display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.hint { color: var(--muted); font-size: 12px; }
.request-item { display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px; padding: 8px; border-radius: 12px; background: #fff3ea; border: 1px solid #ffd5c6; }
.req-name { font-size: 13px; color: #9a3412; }
.req-actions { display: flex; gap: 6px; }
.ok-btn, .no-btn { border: 0; border-radius: 999px; padding: 4px 10px; cursor: pointer; font-size: 12px; }
.ok-btn { background: #16a34a; color: #fff; }
.no-btn { background: #ef4444; color: #fff; }
.item { width: 100%; display: flex; align-items: center; justify-content: space-between; text-align: left; margin-bottom: 6px; border: 0; border-radius: 14px; padding: 10px 12px; cursor: pointer; background: #f1f5ff; color: var(--ink); transition: transform 0.1s ease, background 0.2s ease; }
.item:hover { transform: translateY(-1px); }
.item.active { background: #dbe6ff; box-shadow: inset 0 0 0 1px rgba(15, 91, 255, 0.2); }
.left { display: flex; align-items: center; gap: 8px; }
.dot { width: 8px; height: 8px; border-radius: 50%; background: #9ca3af; }
.dot.online { background: #10b981; }
.badge { min-width: 18px; height: 18px; padding: 0 6px; border-radius: 999px; background: #ef4444; color: #fff; font-size: 12px; display: inline-flex; align-items: center; justify-content: center; }
@media (max-width: 860px) {
  .friend-list { width: 220px; }
}
</style>