<template>
  <div class="alert alert-success" role="alert">
	<h4 class="alert-heading">Well done!</h4>
	<p>{{ msg }}</p>
	<p class="mb-0">Whenever you need to, be sure to use margin utilities to keep things nice and tidy.</p>
  </div>
</template>

<script>
export default {
  name: 'HelloWorld',
  props: {
    msg: String
  }
}
</script>

<!-- Add "scoped" attribute to limit CSS to this component only -->
<style scoped>
</style>
