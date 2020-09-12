<template>
  <div id="app">
	<nav class="nav">
		<router-link class="nav-link" to="/" exact>Home</router-link>
		<router-link class="nav-link" to="/monitor">Monitor</router-link>
		<router-link class="nav-link" to="/about">About</router-link>
	</nav>
    <router-view/>
  </div>
</template>

<style>
</style>