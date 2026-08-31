import type { ApiResponse, LoginData, FriendItem, SearchUserData } from '../types/chat'

async function request<T>(url: string, options?: RequestInit): Promise<ApiResponse<T>> {
  const resp = await fetch(url, options)
  return resp.json()
}

export function login(username: string, password: string) {
  const body = new URLSearchParams({ username, password })
  return request<LoginData>('/api/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body
  })
}

export function register(username: string, password: string, repassword: string) {
  const body = new URLSearchParams({ username, password, repassword })
  return request('/api/register', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body
  })
}

export function userInfo(token: string) {
  return request('/api/userinfo', {
    method: 'GET',
    headers: { Authorization: `Bearer ${token}` }
  })
}

export function friends(token: string) {
  return request<FriendItem[]>('/api/friends', {
    method: 'GET',
    headers: { Authorization: `Bearer ${token}` }
  })
}

export function addFriend(token: string, friend: string) {
  const body = new URLSearchParams({ friend })
  return request('/api/addfriend', {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/x-www-form-urlencoded'
    },
    body
  })
}

export function readFriend(token: string, friend: string) {
  const body = new URLSearchParams({ friend })
  return request('/api/readfriend', {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/x-www-form-urlencoded'
    },
    body
  })
}

export function searchUser(token: string, username: string) {
  const q = encodeURIComponent(username)
  return request<SearchUserData>(`/api/searchuser?username=${q}`, {
    method: 'GET',
    headers: { Authorization: `Bearer ${token}` }
  })
}

export function friendRequests(token: string) {
  return request<string[]>('/api/friendrequests', {
    method: 'GET',
    headers: { Authorization: `Bearer ${token}` }
  })
}

export function verifyFriend(token: string, friend: string, action: 'accept' | 'reject') {
  const body = new URLSearchParams({ friend, action })
  return request('/api/verifyfriend', {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/x-www-form-urlencoded'
    },
    body
  })
}