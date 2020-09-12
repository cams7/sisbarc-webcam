<template>
    <select class="form-control" ref="xclk" placeholder="XCLK MHz" v-model="xclk">
        <option v-for="(xclk, index) in xclks" :value="xclk.value" :key="index">{{xclk.text}}</option>
    </select>    
</template>

<script>
export default {
  name: 'Xclk',
  props: {  
    value: {
      type: Number,
      required: true
    },
    camUrl: String,
    disabled: Boolean
  },
  data () {
    return {
      currentValue: null,
      xclks: [
        { value: 20, text: '20 MHz' },
        { value: 10, text: '10 MHz' },
        { value: 5, text: '5 MHz' }
      ]
    }
  },
  methods: {   
    disableOrEnable: function(disabled) {
      if(disabled) 
        this.$refs['xclk'].setAttribute('disabled', true);
      else
        this.$refs['xclk'].removeAttribute('disabled');      
    } 
  },
  computed: {
    xclk: {
      get() {
          return /*this.currentValue*/this.value
      },
      set(xclk) {             
        this.$ajax.post(
          `${this.camUrl}/api/v1/cam/xclk`, 
          {
            xclk: parseInt(xclk)
          }
        ).then(response => {
          const value = response.data.xclk;
          this.currentValue = value;
          this.$emit('input', value);
        }).catch(error => {
          if(this.currentValue) 
            this.$refs['xclk'].selectedIndex = this.xclks.findIndex(i => i.value === this.currentValue);                         
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
  width:auto;
  height:30px;
  background-color: #626262;
  color: #cacaca;
  border: 1px solid #525252;
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