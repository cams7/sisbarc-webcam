<template>
    <a ref="save-still" :href="href" class="btn btn-sm btn-danger button-icon" :download="download" @click="save">
        <i class="fa fa-image"></i>
    </a>
</template>

<script>
export default {
  name: 'SaveStill',
  props: {
    stream: {},
    disabled: Boolean,
    createCanvas: { type: Function }
  },
  data () {
    return {
      href: '#',
      download: 'capture.jpg'
    }
  },
  methods: { 
    getFormattedDate: function(date) {
      const formattedDate = `${date.getFullYear()}${`0${date.getMonth() + 1}`.slice(-2)}${`0${date.getDate()}`.slice(-2)}${`0${date.getHours()}`.slice(-2)}${`0${date.getMinutes()}`.slice(-2)}${`0${date.getSeconds()}`.slice(-2)}`;
      return formattedDate;
    },
    save: function() {
      const component = this;
      this.createCanvas(
        null, 
        function(camera, canvas) {          
          const dataURL = canvas.toDataURL('image/png');        
          component.href = dataURL;
          component.download = `${component.getFormattedDate(new Date())}.png`;
        }
      );
    },
    disableOrEnable: function(disabled) {
      if(disabled) 
        this.$refs['save-still'].setAttribute('disabled', true);
      else
        this.$refs['save-still'].removeAttribute('disabled');    
    }
  },
  watch: {
    disabled: function(disabled) {
      this.disableOrEnable(disabled);  
    }
  },
  mounted () {
    this.disableOrEnable(this.disabled);
  }
}
</script>

<style scoped>
a[disabled] {
  pointer-events: none;
  opacity: 0.4;
  background-color: #626262;
  border-color: #525252;
}
.button-icon {
  margin-top: -4px;
}
.btn {
  letter-spacing: 0.025rem;
  border-radius: 4px;
  line-height: 18px;
  margin-top: -1px;
  margin-bottom: 1px;
}
.btn-default {
  background-color: #333333;
  color: #cacaca;
  border-color: #525252;
}
.btn-default:active, .btn-default:focus, .btn-default:hover, .btn-default:active:hover {
  color: #cacaca;
  background-color: #626262;
  background-image: none;
  border-color: #525252;
}
</style>