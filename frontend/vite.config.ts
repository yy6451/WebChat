import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  server: {
    port: 5173,
    proxy: {
      '/api': 'http://127.0.0.1:9007',
      '/chat': {
        target: 'ws://127.0.0.1:9007',
        ws: true
      }
    }
  }
})