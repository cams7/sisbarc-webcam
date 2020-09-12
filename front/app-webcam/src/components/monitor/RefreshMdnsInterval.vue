<template>       
    <span class="sitem">
      <select style="float: left;" class="form-control" ref="interval" placeholder="Search Interval Minutes" v-model="interval">
        <option v-for="(interval, index) in refreshMdnsIntervals" :value="interval.value" :key="index">{{interval.text}}</option>
      </select>
      <button type="button" ref="refresh-mdns" :class="refreshMdnsClass" title="Search" @click.stop.prevent="scanMdns">
        <i class="fa fa-search"></i>
      </button>
    </span> 
</template>

<script>
export default {
  name: 'RefreshMdnsInterval',
  props: {  
    value: {
      type: Number,
      required: true
    },
    selectedCamera: {},
    camUrl: String,
    disabled: Boolean
  },
  data () {
    return {
      currentValue: null,
      refreshMdnsIntervals: [
        { value: 0, text: 'Manual' },
        { value: 1, text: '1 Minute' },
        { value: 2, text: '2 Minutes' },
        { value: 5, text: '5 Minutes' },
        { value: 10, text: '10 Minutes' },
        { value: 15, text: '15 Minutes' },
        { value: 30, text: '30 Minutes' },
        { value: 60, text: '1 Hour' }
      ],
      mdnsRefreshTimer: null
    }
  },
  methods: {
    isArray: function(object) {
      return (typeof object === 'object' && object instanceof Array);
    },
    handleCamera: function(cameras, camera) {
      camera.timeout = null;
      cameras.push(camera);
      this.$emit('load-camera-thumbnail', camera); 
    },
    scanMdns: function() {
      this.$ajax.get(`${this.camUrl}/api/v1/mdns`).then(response => {
        const data = response.data;
        let cameras = [];    
        if(this.isArray(data)) {
          for(let i = 0; i < data.length; i++)
            this.handleCamera(cameras, data[i]);  

          this.$emit('set-cameras', cameras);
          if(!this.selectedCamera && cameras.length > 0)
            this.$emit('select-camera', cameras[0]);
        } else
          this.$emit('error', `Data is not Array: ${cameras}`);    
      }).catch(error => {
        this.$emit('error', error);
      })
    },
    setRefreshMdnsInterval: function(interval) {
      this.currentValue = interval;
      if(this.mdnsRefreshTimer) {
        clearInterval(this.mdnsRefreshTimer);
        this.mdnsRefreshTimer = null;
      }

      if(interval > 0) {
        this.$refs['refresh-mdns'].setAttribute('disabled', true);
        this.mdnsRefreshTimer = setInterval(this.scanMdns, interval * 60 * 1000);
      } else
        this.$refs['refresh-mdns'].removeAttribute('disabled');
    },
    disableOrEnable: function(disabled) {  
      if(disabled) {
        this.$refs['interval'].setAttribute('disabled', true);
        this.$refs['refresh-mdns'].setAttribute('disabled', true);
        //this.interval = 0;
      } else {
        this.$refs['interval'].removeAttribute('disabled');
        this.$refs['refresh-mdns'].removeAttribute('disabled');
      }    
    } 
  },
  computed: {
    interval: {
      get() {
        return /*this.currentValue*/this.value
      },
      set(interval) {
        this.setRefreshMdnsInterval(interval);
        this.$emit('input', interval);        
      }
    },
    refreshMdnsClass: function () {
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
    this.scanMdns();
    this.setRefreshMdnsInterval(this.currentValue);
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
  width: 100px;
}
select.form-control {
  height:30px;
  background-color: #626262;
  color: #cacaca;
  border: 1px solid #525252;
  -webkit-appearance:none;
  line-height: 14px;
  cursor:pointer;
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