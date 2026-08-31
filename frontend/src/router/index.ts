import { createRouter, createWebHashHistory } from 'vue-router'
import Login from '../views/Login.vue'
import Register from '../views/Register.vue'
import Chat from '../views/Chat.vue'

const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', redirect: '/login' },
    { path: '/login', component: Login },
    { path: '/register', component: Register },
    { path: '/chat', component: Chat }
  ]
})

router.beforeEach((to, _from, next) => {
  if (to.path === '/chat' && !sessionStorage.getItem('token')) {
    next('/login')
    return
  }
  next()
})

export default router