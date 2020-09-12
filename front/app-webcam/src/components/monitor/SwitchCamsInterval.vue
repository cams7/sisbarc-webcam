<template>       
    <span class="sitem">
      <select style="float: left;" class="form-control" ref="interval" placeholder="" v-model="interval">
        <option v-for="(interval, index) in switchCamsIntervals" :value="interval.value" :key="index">{{interval.text}}</option>
      </select>
      <button type="button" ref="switch-cams" :class="switchCamsClass" title="Switch Cams" @click.stop.prevent="switchCams">
        <i class="fa fa-step-forward"></i>
      </button>                 
    </span> 
</template>

<script>
export default {
  name: 'SwitchCamsInterval',
  props: {  
    value: {
      type: Number,
      required: true
    },
    cameras: Array,
    selectedCamera: {},
    disabled: Boolean
  },
  data () {
    return {
      currentValue: null,
      switchCamsIntervals: [
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
    switchCams: function() {
      const ids = this.cameras ? this.cameras.map(cam => cam.id) : [];
      let numCams = ids.length;

      if(numCams === 0 || (numCams === 1 && this.selectedCamera))
        return;
      
      if(!this.selectedCamera) {
        this.$emit('select-camera', this.cameras[0]);
        return;
      }

      //more than one cams and one is selected!
      for (let i = 0; i < numCams; i++) {
        if(ids[i] === this.selectedCamera.id) {
          if(i === (numCams - 1))
            this.$emit('select-camera', this.cameras[0]);
          else 
            this.$emit('select-camera', this.cameras.filter(cam => cam.id === ids[i + 1])[0]);          
          break;
        }
      }
    },
    setSwitchCamsInterval: function(interval) {
      this.currentValue = interval;
      this.$emit('cancel-cam-switcher');
      if(interval > 0) {
        this.$refs['switch-cams'].setAttribute('disabled', true); 
        this.$emit('cam-switcher-timer', setInterval(this.switchCams, interval * 1000));  
      } else
        this.$refs['switch-cams'].removeAttribute('disabled');      
    },
    disableOrEnable: function(disabled) {
      if(disabled) {
        this.$refs['interval'].setAttribute('disabled', true);
        this.$refs['switch-cams'].setAttribute('disabled', true);
        //this.interval = 0;
      } else {
        this.$refs['interval'].removeAttribute('disabled');
        this.$refs['switch-cams'].removeAttribute('disabled');
      }      
    }
  },
  computed: {
    interval: {
      get() {
        return /*this.currentValue*/this.value
      },
      set(interval) {
        this.setSwitchCamsInterval(interval);
        this.$emit('input', interval);        
      }
    },
    switchCamsClass: function () {
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
    this.setSwitchCamsInterval(this.currentValue);
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