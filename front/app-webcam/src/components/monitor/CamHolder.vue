<template>
  <div :id="itemId" :class="itemClass">
    <h4 :title="camURLTitle" class="text-center">
      {{ cam.instance }}
      <a :ref="camUrl" :href="camUrl" target="_blank" class="btn btn-sm btn-default button-icon">
        <i class="fa fa-cog"></i>
      </a>
    </h4>  
    <div :id="camDetailsId" :class="camDetailsClass">{{ camDetails }}</div>  
    <slot :cam="cam"></slot>
  </div>
</template>

<script>
export default {
  name: 'CamHolder',
  props: {
    cam: {},
    camUrl: String,
    selectedCamera: {},
    isSelectedCamera: Boolean,
    resolutions: {},
    disabled: Boolean
  },
  data () {
    return {
      pixformats: [
        'RGB565',    // 2BPP/RGB565
        'YUV422',    // 2BPP/YUV422
        'GRAYSCALE', // 1BPP/GRAYSCALE
        'JPEG',      // JPEG/COMPRESSED
        'RGB888',    // 3BPP/RGB888
        'RAW',       // RAW
        'RGB444',    // 3BP2P/RGB444
        'RGB555'     // 3BP2P/RGB555
      ]
    }
  },
  methods: { 
    disableOrEnable: function(disabled) {
      if(disabled)
        this.$refs[this.camUrl].setAttribute('disabled', true);
      else
        this.$refs[this.camUrl].removeAttribute('disabled');          
    }
  },
  computed: {
    itemId: function() {
      return `item-${this.cam.id}`;
    },
    camURLTitle: function() {
      return `title-${this.cam.id}`;
    },
    camDetailsId: function() {
      return `tbar-${this.cam.id}`;
    },
    camDetails: function () {
      return `${this.cam.txt.board} ${this.cam.txt.model} ${this.pixformats[this.cam.txt.pixformat]} ${this.resolutions[this.cam.txt.framesize]}`;
    },
    camDetailsClass: function() {
      return {
        'text-center': true,        
        'preview-tbar': true,
        'cam-selected': this.isSelectedCamera && this.selectedCamera && this.cam.id == this.selectedCamera.id
      }
    },
     itemClass: function () {
      return {
        'row': true,
        'disable': this.disabled
      };
    },
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
.disable {
  pointer-events: none;
  opacity: 0.4;
}
a[disabled] {
  pointer-events: none;
  opacity: 0.4;
  background-color: #626262;
  border-color: #525252;
}
.row {
  margin: 5px;
  padding: 5px;
  border: 1px solid #525252;
  border-radius: 4px;
  background-color: #333333;
}
.preview-tbar {
  background-color: #000000;
}
.button-icon {
  margin-top: -4px;
}
.cam-selected {
  width: 100%;    
  color: #155724;
  background-color: #28a745;
  border-color: #c3e6cb;
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
