<template>  
    <button type="button" :class="playStopClass" ref="play-stop" title="Play/Stop" @click.stop.prevent="onCameraButtonPlayOrStop">
      <i :class="playStopIcon"></i>
    </button>  
</template>

<script>
export default {
  name: 'PlayOrStop',
  props: {  
    value: {
      type: Boolean,
      required: true
    },
    stream: {},
    selectedCamera: {},
    camStreamURL: String,
    refreshThumbsInterval: Number,
    disabled: Boolean
  },
  data () {
    return {
      isPlaying: false
    }
  },
  methods: {
    onCameraButtonPlayOrStop: function() {
      if(this.isPlaying) {
        if(this.refreshThumbsInterval > 0)
          this.$emit('refresh-thumbnails');
        else
          this.$emit('load-camera-thumbnail', this.selectedCamera);
        this.stream.src = '';
        this.isPlaying = false;        
      } else {
        this.$emit('switch-cams-interval', 0);  
        this.cancelCamSwitcher();
        this.stream.src = this.camStreamURL;
        this.isPlaying = true;
      }
      this.$emit('input', this.isPlaying);
    },
    cancelCamSwitcher: function () {
      this.$emit('cancel-cam-switcher'); 
    },
    disableOrEnable: function(disabled) { 
      if(disabled) 
        this.$refs['play-stop'].setAttribute('disabled', true);
      else
        this.$refs['play-stop'].removeAttribute('disabled');     
    }
  },
  computed: {
    playing: {
      get() {
        return this.isPlaying
      },
      set(playing) {
        this.$emit('input', playing);        
      }
    },
    playStopIcon: function () {
      return {
        'fa': true,
        'fa-stop': this.isPlaying,
        'fa-play': !this.isPlaying
      };      
    },
    playStopClass: function() {
      return {
        'btn': true, 
        'btn-sm': true, 
        'button-icon': true, 
        'btn-danger': this.isPlaying,
        'btn-success': !this.isPlaying
      };
    }
  },
  watch: {
    disabled: function(disabled) {
      this.disableOrEnable(disabled);  
    }
  },
  mounted () {
    this.isPlaying = this.value;
    this.disableOrEnable(this.disabled);
  }
}
</script>

<style scoped>
button[disabled] {
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