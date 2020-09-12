<template>
    <div class="row" id="console-holder" :style="consoleHolderStyle">
      <div class="col-6">
        <h4 class="text-left">Console</h4>
      </div>
      <div class="col-6">
        <button type="button" class="btn btn-sm btn-dark panel-action float-right button-icon" id="hide-show-console" title="Hide/Show Console" @click.stop.prevent="hideOrShowConsole">
          <i :class="hideShowConsoleIcon"></i>
        </button>
      </div>
      <div class="col-sm-12 well" id="console" :class="consoleClass" title="Console" v-if="!consoleCollapsed">
        <div id="gCodeLog">
          <p style="white-space: pre-line;" :class="messageClass(message.status)" v-for="(message, index) in reversedMessages" :key="index">{{ message.message }}</p>
        </div>            
      </div>
    </div>    
</template>

<script>
export default {
  name: 'ConsoleHolder',
  props: {
    consoleVisible: Boolean,
    messages: Array
  }, 
  methods: { 
    messageClass: function (status) {      
      return {
          'text-muted': !status,
          'text-success': status == 'SUCCESS',
          'text-danger': status == 'DANGER',
          'text-warning': status == 'WARNING',
          'text-info': status == 'INFO'
      };
    },
    hideOrShowConsole: function() {    
      this.$emit('console-visible', !this.consoleVisible);   
    }
  }, 
  computed: {
    consoleCollapsed: function() {
      return !this.consoleVisible;
    },
    consoleHolderStyle: function () {
      return {
        paddingBottom: this.consoleCollapsed ? 0 : ''
      };
    },
    hideShowConsoleIcon: function () {
      return {
        'fa': true,
        'fa-chevron-up': this.consoleCollapsed,
        'fa-chevron-down': !this.consoleCollapsed
      };      
    },
    consoleClass: function () {
      return {
        'collapsed': this.consoleCollapsed
      };
    },
    reversedMessages: function() {
      return this.messages ? Array.from(this.messages).reverse() : [];
    } 
  }
}
</script>

<style scoped>
.well {
  background-color: #242424;
  border: 1px solid #525252;
  width: 100%;
}
/* Console */
#console {
  min-height: 201px;
  max-height: 201px;
  padding: 0 8px;
  overflow-y: auto;
  overflow-x: hidden;
  font-family: monospace, monospace;
  font-size: 0.875rem;
  margin-bottom: 0px;
}
#console p { 
  margin: 0 0 4px; 
}
#console .comment { 
  color: #2C9040; 
}
#console pre {
  color: #aaaaaa;
  background-color: #626262;
  border: 1px solid #525252;
  border-radius: 4px;
}
#console-holder {
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
}
.button-icon {
  margin-top: -4px;
}
.collapsed {
  display: none;
}
/*.panel-action {
  position: absolute;
  right: 6px;
  top: 8px;
}*/
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