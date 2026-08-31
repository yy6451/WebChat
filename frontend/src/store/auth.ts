import { defineStore } from 'pinia'

export const useAuthStore = defineStore('auth', {
  state: () => ({
    // Use sessionStorage so each browser tab keeps its own login session.
    token: sessionStorage.getItem('token') || '',
    username: sessionStorage.getItem('username') || ''
  }),
  actions: {
    setAuth(token: string, username: string) {
      this.token = token
      this.username = username
      sessionStorage.setItem('token', token)
      sessionStorage.setItem('username', username)
    },
    clearAuth() {
      this.token = ''
      this.username = ''
      sessionStorage.removeItem('token')
      sessionStorage.removeItem('username')
    }
  }
})