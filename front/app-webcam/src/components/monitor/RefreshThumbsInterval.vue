<template>       
    <span class="sitem">
      <select style="float: left;" class="form-control" ref="interval" placeholder="" v-model="interval">
        <option v-for="(interval, index) in refreshThumbsIntervals" :value="interval.value" :key="index">{{interval.text}}</option>
      </select>
      <button type="button" ref="refresh-thumbs" :class="refreshThumbsClass" title="Refresh" @click.stop.prevent="refreshThumbnails">
        <i class="fa fa-camera"></i>
      </button>          
    </span> 
</template>

<script>
export default {
  name: 'RefreshThumbsInterval',
  props: {  
    value: {
      type: Number,
      required: true
    },
    stream: {},
    cameras: Array,
    selectedCamera: {},
    playing: Boolean,
    camStreamURL: String,
    disabled: Boolean
  },
  data () {
    return {
      currentValue: null,
      refreshThumbsIntervals: [
        { value: 0, text: 'OFF' },
        { value: 1, text: '1s' },
        { value: 2, text: '2s' },
        { value: 5, text: '5s' },
        { value: 10, text: '10s' },
        { value: 30, text: '30s' },
        { value: 60, text: '60s' }
      ]
    }
  },
  methods: {
    refreshThumbnails: function() {
      this.$emit('refresh-thumbnails');
    },
    setRefreshThumbsInterval: function(interval) {
      this.currentValue = interval;

      window.stop();
      //reastart playback
      if(this.selectedCamera && this.playing)
        this.stream.src = this.camStreamURL;
      
      //clear all timeouts
      for(let i = 0; i < this.cameras.length; i++) {
        let camera = this.cameras[i];
        if(camera.timeout) {
          clearTimeout(camera.timeout);
          camera.timeout = null;
        }
      }

      if(interval > 0) {
        this.$refs['refresh-thumbs'].setAttribute('disabled', true);
        this.refreshThumbnails();
      } else 
        this.$refs['refresh-thumbs'].removeAttribute('disabled');      
    },
    disableOrEnable: function(disabled) {
      if(disabled) {
        this.$refs['interval'].setAttribute('disabled', true);
        this.$refs['refresh-thumbs'].setAttribute('disabled', true);
        //this.interval = 0;
      } else {
        this.$refs['interval'].removeAttribute('disabled');
        this.$refs['refresh-thumbs'].removeAttribute('disabled');
      }      
    }
  },
  computed: {
    interval: {
      get() {
        return /*this.currentValue*/this.value
      },
      set(interval) {
        this.setRefreshThumbsInterval(interval);
        this.$emit('input', interval);        
      }
    },
    refreshThumbsClass: function () {
      return {
        'btn': true, 
        'btn-sm': true, 
        'btn-success': true, 
        'fitem': true,
        'button-icon': true
      };
    }
  },
  watch: {
    disabled: function(disabled) {
      this.disableOrEnable(disabled);  
    }
  },
  mounted () {
    this.currentValue = this.value;
    this.setRefreshThumbsInterval(this.currentValue);
    this.disableOrEnable(this.disabled);
  }
}
</script>

<style scoped>
*[disabled] {
  pointer-events: none;
  opacity: 0.4;
  background-color: #626262;
  border-color: #525252;
}
.sitem, .fitem {
  position: relative;
  border-radius: 0;
}
.sitem {
  z-index: 2;
  margin-right: -2px;
  border-top-left-radius: 4px;
  border-bottom-left-radius: 4px;
  padding-right: 5px;
}
.fitem {
  z-index: 2;
  margin-left: -2px;
  border-top-right-radius: 4px;
  border-bottom-right-radius: 4px;
}
select {
  padding: 5px;
  border: 1px solid #525252;
  border-radius: 4px;
  background-color: #333333;
  width: 54px;
}
select.form-control {
  height: 30px;
  background-color: #626262;
  color: #cacaca;
  border: 1px solid #525252;
  -webkit-appearance: none;
  line-height: 14px;
  cursor: pointer;
}
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
.form-control:focus, .btn:focus {
  outline: none;
  box-shadow: none;
}
.form-control {
  display: inline-block;
}
</style>