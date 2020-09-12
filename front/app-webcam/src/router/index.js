import Vue from 'vue'
import VueRouter from 'vue-router'
import Home from '../views/Home.vue'
import Monitor from '../views/Monitor.vue'

Vue.use(VueRouter)

const routes = [  
  {
    path: '/monitor',
    name: 'Monitor',
    component: Monitor
  },
  {
    path: '/about',
    name: 'About',
    component: () => import('../views/About.vue')
  },
  {
    path: '/',
    name: 'Home',
    component: Home
  }
]

const router = new VueRouter({
  mode: 'history',
  base: process.env.BASE_URL,
  routes: routes
})

export default router
