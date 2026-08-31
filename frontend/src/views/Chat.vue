<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../store/auth'
import FriendList from '../components/FriendList.vue'
import ChatRoom from '../components/ChatRoom.vue'
import { ChatSocket } from '../api/ws'
import { addFriend, friendRequests, friends as fetchFriends, readFriend, searchUser, userInfo, verifyFriend } from '../api/http'
import type { FriendItem, SearchUserData, WsMessage } from '../types/chat'

const auth = useAuthStore()
const router = useRouter()

const users = ref<FriendItem[]>([])
const current = ref('room_default')
const state = reactive({ wsState: 'connecting' })
const conversations = reactive<Record<string, Array<{ role: 'self' | 'other' | 'system'; meta: string; content: string }>>>({ room_default: [] })
const onlineSet = ref<Set<string>>(new Set())
const pendingRequests = ref<string[]>([])
const friendSearch = reactive<{
  searching: boolean
  message: string
  result: { username: string; exists: boolean; isFriend: boolean } | null
}>({
  searching: false,
  message: '',
  result: null
})

const wsUrl = computed(() => {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
  const token = auth.token
  return `${proto}//${location.host}/chat?token=${encodeURIComponent(token)}`
})

let socket: ChatSocket | null = null

function ensure(key: string) {
  if (!conversations[key]) conversations[key] = []
  return conversations[key]
}

function increaseUnread(username: string) {
  const idx = users.value.findIndex((u) => u.username === username)
  if (idx === -1) return
  users.value[idx].unread += 1
}

function isFriend(username: string) {
  return users.value.some((u) => u.username === username)
}

function clearUnread(username: string) {
  const idx = users.value.findIndex((u) => u.username === username)
  if (idx >= 0) users.value[idx].unread = 0
}

async function markRead(username: string) {
  if (!username || username === 'room_default') return
  try {
    await readFriend(auth.token, username)
  } catch {
    // ignore network errors
  }
}

function syncOnlineFlags() {
  users.value = users.value.map((u) => ({
    ...u,
    online: onlineSet.value.has(u.username)
  }))
}

async function loadFriends() {
  const resp = await fetchFriends(auth.token)
  if (resp.code !== 0 || !resp.data) return
  const incoming = resp.data as FriendItem[]
  users.value = incoming
    .filter((x) => x.username !== auth.username)
    .map((x) => ({ username: x.username, unread: x.unread ?? 0, online: onlineSet.value.has(x.username) }))
}

async function loadFriendRequests() {
  try {
    const resp = await friendRequests(auth.token)
    if (resp.code === 0 && resp.data) {
      pendingRequests.value = resp.data as string[]
    }
  } catch {
    pendingRequests.value = []
  }
}

function onMessage(msg: WsMessage) {
  if ((msg.type === 'online_users' || msg.type === 'online_list') && msg.users) {
    onlineSet.value = new Set(msg.users.filter((u) => u && u !== auth.username))
    syncOnlineFlags()
    return
  }
  if (msg.type === 'heartbeat') return
  if (msg.type === 'system') {
    ensure('room_default').push({ role: 'system', meta: '系统', content: msg.content })
    return
  }
  if (msg.type === 'private') {
    const key = msg.from === auth.username ? msg.to : msg.from
    const friend = isFriend(key)
    if (!friend && msg.from !== auth.username) {
      ensure('room_default').push({ role: 'system', meta: '系统', content: `收到来自非好友 ${msg.from} 的私聊，已忽略。请先添加好友。` })
      return
    }
    ensure(key).push({ role: msg.from === auth.username ? 'self' : 'other', meta: msg.from, content: msg.content })
    if (msg.from !== auth.username && current.value !== key) {
      increaseUnread(key)
    } else if (msg.from !== auth.username && current.value === key) {
      clearUnread(key)
      markRead(key)
    }
    return
  }
  ensure('room_default').push({ role: msg.from === auth.username ? 'self' : 'other', meta: msg.from, content: msg.content })
}

async function onSearchFriend(username: string) {
  const target = username.trim()
  if (!target) {
    friendSearch.message = '请输入用户名'
    friendSearch.result = null
    return
  }
  if (target === auth.username) {
    friendSearch.message = '不能添加自己'
    friendSearch.result = { username: target, exists: true, isFriend: false }
    return
  }

  friendSearch.searching = true
  try {
    const resp = await searchUser(auth.token, target)
    if (resp.code === 0 && resp.data) {
      const data = resp.data as SearchUserData
      const exists = !!data.exists
      const already = exists && isFriend(target)
      friendSearch.result = { username: target, exists, isFriend: already }
      if (!exists) {
        friendSearch.message = '用户不存在'
      } else if (already) {
        friendSearch.message = '已是好友'
      } else {
        friendSearch.message = '用户存在，可添加好友'
      }
    } else {
      friendSearch.result = null
      friendSearch.message = resp.msg || '搜索失败'
    }
  } catch {
    friendSearch.result = null
    friendSearch.message = '搜索失败，请稍后重试'
  } finally {
    friendSearch.searching = false
  }
}

async function onAddFriend(username: string) {
  const target = username.trim()
  if (!target || target === auth.username) return
  try {
    const resp = await addFriend(auth.token, target)
    if (resp.code === 0) {
      friendSearch.message = '请求已发送，等待对方验证通过后可私聊'
      friendSearch.result = { username: target, exists: true, isFriend: false }
    } else {
      friendSearch.message = resp.msg || '添加失败'
    }
  } catch {
    friendSearch.message = '添加失败，请稍后重试'
  }
}

async function onVerifyFriend(payload: { friend: string; action: 'accept' | 'reject' }) {
  try {
    const resp = await verifyFriend(auth.token, payload.friend, payload.action)
    if (resp.code === 0) {
      await loadFriendRequests()
      await loadFriends()
      if (payload.action === 'accept') {
        current.value = payload.friend
        friendSearch.message = `已通过 ${payload.friend} 的好友请求`
      }
    } else {
      friendSearch.message = resp.msg || '处理失败'
    }
  } catch {
    friendSearch.message = '处理失败，请稍后重试'
  }
}

function onState(s: 'open' | 'close' | 'error' | 'connecting') {
  state.wsState = s
}

function send(msg: WsMessage) {
  socket?.send(msg)
}

function logout() {
  socket?.close()
  auth.clearAuth()
  router.push('/login')
}

onMounted(() => {
  if (!auth.token) {
    router.push('/login')
    return
  }

  userInfo(auth.token)
    .then((resp) => {
      if (resp.code !== 0 || !resp.data) {
        throw new Error(resp.msg || 'invalid token')
      }
      const data = resp.data as { username: string }
      if (data.username && data.username !== auth.username) {
        auth.setAuth(auth.token, data.username)
      }
      socket = new ChatSocket(wsUrl.value, onMessage, onState)
      socket.connect()
      loadFriends()
      loadFriendRequests()
    })
    .catch(() => {
      auth.clearAuth()
      router.push('/login')
    })
})

onBeforeUnmount(() => {
  socket?.close()
})
</script>

<template>
  <main class="chat-page">
    <header class="topbar">
      <div class="user">
        <div class="avatar">{{ auth.username?.slice(0, 1) || 'U' }}</div>
        <div>
          <div class="name">{{ auth.username }}</div>
          <div class="sub">实时聊天室</div>
        </div>
      </div>
      <div class="status">连接：<span :class="['pill', state.wsState]">{{ state.wsState }}</span></div>
      <button class="ghost" @click="logout">退出</button>
    </header>
    <div class="body">
      <FriendList
        :users="users"
        :current="current"
        :me="auth.username"
        :pending-requests="pendingRequests"
        :searching="friendSearch.searching"
        :search-message="friendSearch.message"
        :search-result="friendSearch.result"
        @search="onSearchFriend"
        @add="onAddFriend"
        @verify="onVerifyFriend"
        @select="(u) => { current = u; clearUnread(u); markRead(u) }"
      />
      <ChatRoom :target="current" :username="auth.username" :messages="conversations[current] || []" @send="send" />
    </div>
  </main>
</template>

<style scoped>
.chat-page { height: 100vh; display: flex; flex-direction: column; background: rgba(255, 255, 255, 0.6); backdrop-filter: blur(10px); overflow: hidden; }
.topbar { height: 64px; background: linear-gradient(135deg, #0f5bff, #3059ff); color: #fff; display: flex; align-items: center; justify-content: space-between; padding: 0 18px; box-shadow: 0 10px 30px rgba(15, 91, 255, 0.25); }
.user { display: flex; align-items: center; gap: 10px; }
.avatar { width: 36px; height: 36px; border-radius: 50%; background: rgba(255, 255, 255, 0.2); display: grid; place-items: center; font-weight: 700; text-transform: uppercase; }
.name { font-weight: 600; }
.sub { font-size: 12px; opacity: 0.8; }
.status { font-size: 13px; }
.pill { padding: 4px 10px; border-radius: 999px; background: rgba(255, 255, 255, 0.2); margin-left: 6px; text-transform: capitalize; }
.pill.open { background: rgba(16, 185, 129, 0.8); }
.pill.error, .pill.close { background: rgba(239, 68, 68, 0.8); }
.ghost { border: 1px solid rgba(255, 255, 255, 0.5); background: transparent; color: #fff; border-radius: 999px; padding: 6px 14px; cursor: pointer; }
.body { flex: 1; display: flex; background: #fff; min-height: 0; overflow: hidden; border-top: 1px solid var(--stroke); }
@media (max-width: 860px) {
  .status { display: none; }
}
</style>