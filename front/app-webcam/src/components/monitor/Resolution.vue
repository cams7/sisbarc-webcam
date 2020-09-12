<template>
    <select class="form-control" ref="resolution" placeholder="Resolution" v-model="resolution" >
      <option v-for="(resolution, index) in selectedResolutions" :value="resolution.value" :key="index">{{resolution.text}}</option>            
    </select>   
</template>

<script>
export default {
  name: 'Resolution',
  props: {  
    selectedResolutions: Array,
    value: {
      type: Number,
      required: true
    },
    camUrl: String,
    disabled: Boolean
  },
  data () {
    return {
      currentValue: null
    }
  },
  methods: {
    disableOrEnable: function(disabled) {
      if(disabled) 
        this.$refs['resolution'].setAttribute('disabled', true);
      else
        this.$refs['resolution'].removeAttribute('disabled');      
    }   
  },
  computed: {
    resolution: {
      get() {
        return /*this.currentValue*/this.value
      },
      set(resolution) {             
        this.$ajax.post(
          `${this.camUrl}/api/v1/cam/control`, 
          {
            framesize: parseInt(resolution)
          }
        ).then(response => {
          const value = response.data.framesize;
          this.currentValue = value;
          this.$emit('input', value);
        }).catch(error => {
          if(this.currentValue) 
            this.$refs['resolution'].selectedIndex = this.selectedResolutions.findIndex(i => i.value === this.currentValue);                         
          this.$emit('error', error);
        });                                                     
      }
    }
  },
  watch: {
    disabled: function(disabled) {
      this.disableOrEnable(disabled);  
    }
  },
  mounted () {
    this.currentValue = this.value;
    this.disableOrEnable(this.disabled);
  }
}
</script>

<style scoped>
select {
  width:170px;
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
select[disabled] {
  pointer-events: none;
  opacity: 0.4;
  background-color: #626262;
  border-color: #525252;
}
</style>