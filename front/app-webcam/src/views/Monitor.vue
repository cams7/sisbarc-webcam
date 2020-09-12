<template>
  <div id="page">
    <div class="row">
      <div id="cam-holder">
        <div class="row" id="mdns-holder">
          <RefreshMdnsInterval 
            v-model="camHolder.refreshMdnsInterval" 
            :selectedCamera="camHolder.selectedCamera"
            :camUrl="getCamURL({ip:'192.168.100.36', port:80})"
            :disabled="!camHolder.selectedCamera || camHolder.playing"
            @set-cameras="camHolder.cameras = $event"
            @select-camera="selectCamera($event)"
            @load-camera-thumbnail="loadCameraThumbnail($event)"
            @error="gException($event)">
          </RefreshMdnsInterval>          
          <RefreshThumbsInterval 
            v-model="camHolder.refreshThumbsInterval"
            :stream="$refs['stream']"
            :cameras="camHolder.cameras"
            :selectedCamera="camHolder.selectedCamera"
            :playing="camHolder.playing"
            :camStreamURL="getCamStreamURL(camHolder.selectedCamera)"
            :disabled="!camHolder.selectedCamera || camHolder.playing" 
            @refresh-thumbnails="refreshThumbnails">
          </RefreshThumbsInterval>
          <SwitchCamsInterval 
            v-model="camHolder.switchCamsInterval" 
            :cameras="camHolder.cameras"
            :selectedCamera="camHolder.selectedCamera"
            :disabled="!camHolder.selectedCamera || camHolder.cameras.length == 1"
            @select-camera="selectCamera($event)"
            @cancel-cam-switcher="cancelCamSwitcher"
            @cam-switcher-timer="camHolder.camSwitcherTimer = $event">
          </SwitchCamsInterval>
        </div> 
        <CamHolder 
          ref="cam-holder-items"
          :resolutions="streamHolder.resolutions" 
          :selectedCamera="camHolder.selectedCamera"
          :isSelectedCamera="camHolder.isSelectedCamera" 
          v-for="(cam, index) in camHolder.cameras" 
          :key="index" 
          :cam="cam" 
          :camUrl="getCamURL(cam)"
          :disabled="camHolder.playing" 
          v-slot:default="slotProps">
            <img :ref="cam.id" crossorigin style="width: 100%" @click.stop.prevent="changeCamera(slotProps.cam)">                      
        </CamHolder>          
      </div>
      <div id="stream-holder">
        <div class="row" :class="controlsHolderClass" id="controls-holder">
          <SaveStill 
            :stream="$refs['stream']"
            :disabled="streamHolder.disableControlsHolder || !camHolder.playing"
            :createCanvas="createCanvas" 
            @error="gException($event)">
          </SaveStill>
          <PlayOrStop
            ref="play-stop"
            v-model="camHolder.playing" 
            :stream="$refs['stream']"
            :selectedCamera="camHolder.selectedCamera" 
            :camStreamURL="getCamStreamURL(camHolder.selectedCamera)"
            :refreshThumbsInterval="camHolder.refreshThumbsInterval"
            :disabled="streamHolder.disableControlsHolder"
            @switch-cams-interval="camHolder.switchCamsInterval = $event"
            @cancel-cam-switcher="cancelCamSwitcher"
            @refresh-thumbnails="refreshThumbnails"
            @load-camera-thumbnail="loadCameraThumbnail($event)">
          </PlayOrStop>
          <Resolution 
            v-model="streamHolder.resolution"
            :camUrl="getCamURL(camHolder.selectedCamera)" 
            :selectedResolutions="streamHolder.selectedResolutions"
            :disabled="streamHolder.disableControlsHolder" 
            @error="gException($event)">
          </Resolution>
          <Xclk 
            v-model="streamHolder.xclk" 
            :camUrl="getCamURL(camHolder.selectedCamera)"
            :disabled="streamHolder.disableControlsHolder" 
            @error="gException($event)">
          </Xclk>
        </div>
        <div class="row" id="view-holder" :style="viewHolderStyle">
          <div id="stream-win" :class="streamWinClass">
            <img id="stream" ref="stream" crossorigin>              
          </div>
        </div>
        <ConsoleHolder 
          :consoleVisible="streamHolder.consoleVisible"  
          :status="consoleHolder.status" 
          :messages="consoleHolder.messages"
          @console-visible="hideOrShowConsole($event)">
        </ConsoleHolder>        
      </div>
    </div>
  </div>
</template>

<script>
import ConsoleHolder from '@/components/monitor/ConsoleHolder.vue'
import CamHolder from '@/components/monitor/CamHolder.vue'
import SaveStill from '@/components/monitor/SaveStill.vue'
import Xclk from '@/components/monitor/Xclk.vue'
import Resolution from '@/components/monitor/Resolution.vue'
import RefreshMdnsInterval from '@/components/monitor/RefreshMdnsInterval.vue'
import RefreshThumbsInterval from '@/components/monitor/RefreshThumbsInterval.vue'
import SwitchCamsInterval from '@/components/monitor/SwitchCamsInterval.vue'
import PlayOrStop from '@/components/monitor/PlayOrStop.vue'

export default {
  name: 'Monitor',  
  components: {
    ConsoleHolder,
    CamHolder,
    SaveStill,
    Xclk,
    Resolution,
    RefreshMdnsInterval,
    RefreshThumbsInterval,
    SwitchCamsInterval,
    PlayOrStop
  },  
  data () {
    return {
      camHolder: {
        refreshMdnsInterval: 0,
        refreshThumbsInterval: 0,
        switchCamsInterval: 0,
        cameras: [], 
        selectedCamera: null,   
        isSelectedCamera: false,
        camSwitcherTimer: null,
        playing: false
      },
      streamHolder: {
        resolutions: {
          // <!-- 5MP -->
          21:'QSXGA(2560x1920)',
          20:'P FHD(1080x1920)',
          19:'WQXGA(2560x1600)',
          18:'QHD(2560x1440)',
          // <!-- 3MP -->
          17:'QXGA(2048x1564)',
          16:'P 3MP(864x1564)',
          15:'P HD(720x1280)',
          14:'FHD(1920x1080)',
          // <!-- 2MP -->
          13:'UXGA(1600x1200)',
          12:'SXGA(1280x1024)',
          11:'HD(1280x720)',
          10:'XGA(1024x768)',
          9:'SVGA(800x600)',
          // <!-- VGA -->
          8:'VGA(640x480)',
          7:'HVGA(480x320)',
          6:'CIF(400x296)',
          5:'QVGA(320x240)',
          4:'240x240',
          3:'HQVGA(240x176)',
          2:'QCIF(176x144)',
          1:'QQVGA(160x120)',
          0:'96x96'
        },
        selectedResolutions: [],
        resolution: 8,
        xclk: 10,
        consoleVisible: true,
        disableControlsHolder: true
      },
      consoleHolder: {
        messageStatus: {
          success: 'SUCCESS',
          danger: 'DANGER',
          warning: 'WARNING',
          info: 'INFO'
        },
        messages: []
      }          
    }
  },
  methods: {     
    getConsoleDate: function(date) {
      const options = { 
        timeZone: 'America/Sao_Paulo', 
        timeZoneName: 'short', 
        hour12: false, 
        weekday: 'long', 
        year: 'numeric', 
        month: 'long', 
        day: 'numeric',  
        hour: 'numeric', 
        minute: 'numeric', 
        second: 'numeric' 
      };
      return `${date.toLocaleString('pt-BR', options)}`;
    },
    addToConsole: function(message, status) {
      this.consoleHolder.messages.push({
        message: `* ${this.getConsoleDate(new Date())}:\n${message}`, 
        status: status
      });
    },
    gDebug: function (message) { 
      this.addToConsole(message, undefined); 
    },
    gLog: function(message) { 
      this.addToConsole(message, this.consoleHolder.messageStatus.info); 
    },
    gInfo: function(message) { 
      this.addToConsole(message, this.consoleHolder.messageStatus.success); 
    },
    gWarn: function(message) { 
      this.addToConsole(message, this.consoleHolder.messageStatus.warning); 
    },
    gError: function(message) { 
      this.addToConsole(message, this.consoleHolder.messageStatus.danger); 
    },
    gException: function (e) {
      const message = (e instanceof Error) ? `${e.name}: '${e.message}', line: ${e.line}, , stack:\n${e.stack}` : `Error: '${e}'`;
      this.gError(message);
    },
    createCanvas: function(camera, callback) {   
      const stream = this.$refs['stream'];
      const canvas = document.createElement('canvas');
      canvas.width = stream.naturalWidth;
      canvas.height = stream.naturalHeight;
      document.body.appendChild(canvas);
      const context = canvas.getContext('2d');
      context.drawImage(stream, 0, 0);
      try {      
        callback(camera, canvas);
      } catch (e) {
        this.gException(e);
      }
      canvas.parentNode.removeChild(canvas);
    },
    getCamURL: function (camera) {
      if(!camera)
        return undefined;

      let host = `http://${camera.ip}`;
      if(camera.port !== 80)
        return `${host}:${camera.port}`; 

      return host;
      //return '';
    },
    getCamStreamURL: function(camera) {
      if(!camera)
        return undefined;

      let host = `http://${camera.ip}`;
      let port = camera.txt.stream_port ? camera.txt.stream_port : camera.port;
      if(port !== 80)
        return `${host}:${port}/cam/stream`;
      
      return `${host}/cam/stream`;
    },
    hideOrShowConsole: function(consoleVisible) {   
      this.streamHolder.consoleVisible = consoleVisible;     
    },  
    cancelCamSwitcher: function () {
      if(this.camHolder.camSwitcherTimer) {
        clearInterval(this.camHolder.camSwitcherTimer);
        this.camHolder.camSwitcherTimer = null;
      }
    },
    playOrStop() {
      const ref = 'play-stop';
      this.$refs[ref].$refs[ref].click();
    },
    removeCamera: function(camera) {
      if(!camera)
        return;
      
      const item = this.$refs['cam-holder-items'].filter(item => item.$el.id == `item-${camera.id}`)[0].$el;
      this.gWarn(`Remove Camera[${camera.id}]: ${camera.instance}`);
      item.parentElement.removeChild(item);

      if(camera.timeout) {
        clearTimeout(camera.timeout);
        camera.timeout = null;
      }

      this.camHolder.cameras = this.camHolder.cameras.filter(cam => cam.id != camera.id);

      if(this.camHolder.selectedCamera && this.camHolder.selectedCamera.id == camera.id) {
        this.camHolder.selectedCamera = null;
        if(this.camHolder.playing)
          this.playOrStop();
        
        this.streamHolder.disableControlsHolder = true;
      }
    },
    fillResolutionSelect: function(model) {
      let maxResolution = 8;
      switch(model) {
        case 'OV2640': 
          maxResolution = 13; 
          break;
        case 'OV3660': 
          maxResolution = 17; 
          break;
        case 'OV5640': 
          maxResolution = 21; 
          break;
        default: 
          break;
      }
      this.streamHolder.selectedResolutions = [];

      for(let i=0; i<=maxResolution; i++) 
        this.streamHolder.selectedResolutions.push({value: i, text: this.streamHolder.resolutions[i]});
    },    
    selectCamera: function(camera) {      
      if(this.camHolder.selectedCamera && this.camHolder.selectedCamera.id == camera.id)
        return;

      if(this.camHolder.selectedCamera) {
        this.camHolder.isSelectedCamera = false;
        if(this.camHolder.playing)                     
          this.playOrStop();        
      }

      this.camHolder.selectedCamera = camera;
      const view = this;
      setTimeout(function() { 
        view.streamHolder.disableControlsHolder = true;
        view.camHolder.playing = false;
        
        const stream = view.$refs['stream'];
        const camImage = view.$refs[view.camHolder.selectedCamera.id][0];       
        stream.src = camImage.src;

        view.fillResolutionSelect(view.camHolder.selectedCamera.txt.model);
        view.streamHolder.resolution = parseInt(view.camHolder.selectedCamera.txt.framesize) || 8;  

        const $refs = view.$refs;
        view.$ajax.get(`${view.getCamURL(view.camHolder.selectedCamera)}/api/v1/cam/status`).then(response => {        
          const stream = $refs['stream'];
          if(!stream.src) {                    
            const camImage = $refs[view.camHolder.selectedCamera.id][0];   
            stream.src = camImage.src;
          }
          
          let data = response.data;

          view.streamHolder.resolution = data.framesize || 8;
          view.streamHolder.xclk = data.xclk || 10;
          
          view.camHolder.isSelectedCamera = true;  
          
          view.streamHolder.disableControlsHolder = false;
        }).catch(error => {
          if(view.camHolder.selectedCamera)
            view.removeCamera(view.camHolder.selectedCamera);
          view.gError(error);
        });
      }, 100);      
    },    
    getCb: function(camera) {
      const view = this;
      return function() {
        view.loadCameraThumbnail(camera);
      }
    },
    copyThumbnailFromPlayer: function(camera) {  
      const view = this;
      this.createCanvas(
        camera, 
        function(camera, canvas) {            
          canvas.toBlob(function(data) {
            if(data) {
              const camImage = view.$refs[camera.id][0];
              camImage.src = window.URL.createObjectURL(data);            
            }
          }, 'image/jpeg', 0.80);
        }
      );       
    },
    loadRemoteThumbnail: function(camera) {
      if(!camera || !this.camHolder.selectedCamera)
        return;

      this.$ajax.get(
        `${this.getCamURL(this.camHolder.selectedCamera)}/api/v1/cam/capture`,
        { responseType: 'arraybuffer' }
      ).then(response => {        
        const camImage = this.$refs[camera.id][0];
        const headers = response.headers;
        const data = response.data;
        const blob = new Blob(
          [data],
          { type: headers['content-type'] }
        );
        camImage.src = window.URL.createObjectURL(blob);
        if(this.camHolder.selectedCamera.id == camera.id && !this.camHolder.playing) {
          const stream = this.$refs['stream'];
          //copy thumbnail to player
          stream.src = camImage.src;
        }
        const to = this.camHolder.refreshThumbsInterval;
        if(to > 0) 
          camera.timeout = setTimeout(this.getCb(camera), to * 1000);         
      }).catch(error => {
        this.removeCamera(this.camHolder.selectedCamera);
        this.gError(error);
      });
    },
    loadCameraThumbnail: function(camera) {
      if(!camera)
        return;

      if(camera.timeout) {
        clearTimeout(camera.timeout);
        camera.timeout = null;
      }
      
      if(this.camHolder.selectedCamera && this.camHolder.selectedCamera.id == camera.id && this.camHolder.playing) {
        this.copyThumbnailFromPlayer(camera);
        const to = this.camHolder.refreshThumbsInterval;
        if(to > 0) 
          camera.timeout = setTimeout(this.getCb(camera), to * 1000);        
      } else 
        this.loadRemoteThumbnail(camera);      
    },
    refreshThumbnails: function() {
      for(let i = 0; i < this.camHolder.cameras.length; i++) {
        let camera = this.camHolder.cameras[i];
        this.loadCameraThumbnail(camera);
      }
    },    
    changeCamera: function(camera) {
      this.camHolder.switchCamsInterval = 0;
      this.cancelCamSwitcher();
      this.camHolder.selectedCamera = camera;
    }
  },
  computed: {     
    controlsHolderClass: function () {
      return {
        'disable': this.streamHolder.disableControlsHolder
      };
    },    
    viewHolderStyle: function() {
      return {
        bottom: !this.streamHolder.consoleVisible ? '46px' : ''
      };
    },
    streamWinClass : function () {
      return {
        'disable': !this.camHolder.playing
      };
    }
  },
  mounted () {
  },
  beforeUpdate() {
  },
  updated() {
  }
}
</script>

<style scoped>
/*html {
  font-size: 16px;
  overflow-y: auto;
}
body {
  color: #cacaca;
  background-color: #242424;
  user-select: text;
}*/
div#page {
  padding-left: 20px;
}
/*h3,h4 {
  color: #cacaca;
  font-weight: 300;
  font-size: 18px;
}
h3 {
  margin-bottom: 20px;
  font-size: 1.5rem;
}
h4 { 
  margin-top: 4px; 
}
h4 a {
  color: #cacaca;
}
h4 a.btn {
  margin-top: -6px;
  float:right;
}
h4 a:focus, h4 a:hover {
  color: #cacaca;
  text-decoration: none;
}*/
.disable {
  pointer-events: none;
  opacity: 0.4;
}
#controls-holder {
  /*position: absolute;
  left: 0;
  top: 0;
  right: 0;*/
  height: 42px;
}
/*#view-holder {
  position: absolute;
  left: 0;
  top: 47px;
  right: 0;
  bottom: 251px;
}*/
#cam-holder {
  width: 342px;
  /*left: 0px;*/
  overflow-x: hidden;
  overflow-y: auto;
}
/*#stream-holder {
  left: 342px;
  right: 0px;
}*/
#cam-holder, #stream-holder {
  padding: 0px;
  height: 100%;
  /*position: absolute;
  top: 0px;
  bottom: 0px;*/
}
#cam-holder .row, #stream-holder .row {
  margin: 5px;
  padding: 5px;
  border: 1px solid #525252;
  border-radius: 4px;
  background-color: #333333;
}
#cam-holder img {
  cursor: pointer;
}
#stream {
  max-width: 100%;
  max-height: 100%;
  height: 100%;
  width:100%;
  object-fit: contain;
}
#stream-win {
  width: 100%;
  height: 100%;
  text-align: center;
  background-color: black;
}
</style>
