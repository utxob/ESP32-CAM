#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"
#include "SD_MMC.h"
#include "FS.h"

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Pin definitions for AI-Thinker ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASH_GPIO_NUM     4
#define BUTTON_GPIO_NUM   13  // Flash control button
#define RECORD_GPIO_NUM   12  // Record toggle button

// Camera settings
int frameRate = 10;
framesize_t frameSize = FRAMESIZE_SVGA;
bool flashState = false;
bool recording = false;
File videoFile;

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

void startCameraServer();
void setupFlash();
void setupButtons();
void toggleFlash();
void toggleRecording();
void saveFrameToSD(camera_fb_t *fb);
static esp_err_t stream_handler(httpd_req_t *req);
static esp_err_t cmd_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);
static esp_err_t capture_handler(httpd_req_t *req);

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  
  // Initialize camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = frameSize;
  config.jpeg_quality = 12;
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Initialize SD card
  if(!SD_MMC.begin()){
    Serial.println("SD Card Mount Failed");
    return;
  }
  
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD Card attached");
    return;
  }

  // Setup flash and buttons
  setupFlash();
  setupButtons();

  // Connect to WiFi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  Serial.print("Camera Stream Ready! Go to: http://");
  Serial.println(WiFi.localIP());

  // Start web server
  startCameraServer();
}

void loop() {
  // Handle button presses
  static unsigned long lastDebounceTime = 0;
  static unsigned long debounceDelay = 200;
  
  if (digitalRead(BUTTON_GPIO_NUM) == LOW && millis() - lastDebounceTime > debounceDelay) {
    lastDebounceTime = millis();
    toggleFlash();
  }
  
  if (digitalRead(RECORD_GPIO_NUM) == LOW && millis() - lastDebounceTime > debounceDelay) {
    lastDebounceTime = millis();
    toggleRecording();
  }
  
  delay(10);
}

void setupFlash() {
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, flashState);
}

void setupButtons() {
  pinMode(BUTTON_GPIO_NUM, INPUT_PULLUP);
  pinMode(RECORD_GPIO_NUM, INPUT_PULLUP);
}

void toggleFlash() {
  flashState = !flashState;
  digitalWrite(FLASH_GPIO_NUM, flashState);
  Serial.println(flashState ? "Flash ON" : "Flash OFF");
}

void toggleRecording() {
  recording = !recording;
  
  if (recording) {
    // Create new video file with timestamp
    char filename[32];
    sprintf(filename, "/video_%d.mjpeg", millis());
    videoFile = SD_MMC.open(filename, FILE_WRITE);
    if(!videoFile){
      Serial.println("Failed to create file");
      recording = false;
      return;
    }
    Serial.println("Recording started");
  } else {
    if (videoFile) {
      videoFile.close();
      Serial.println("Recording stopped");
    }
  }
}

void saveFrameToSD(camera_fb_t *fb) {
  if (recording && videoFile) {
    // Write MJPEG frame
    videoFile.write(fb->buf, fb->len);
  }
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  size_t fb_len = 0;
  if (fb->format == PIXFORMAT_JPEG) {
    fb_len = fb->len;
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  } else {
    jpg_chunking_t jchunk = {req, 0};
    res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
    httpd_resp_send_chunk(req, NULL, 0);
    fb_len = jchunk.len;
  }
  
  esp_camera_fb_return(fb);
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];
  
  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){
    return res;
  }

  while(true){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if(fb->format != PIXFORMAT_JPEG){
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if(!jpeg_converted){
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    
    // Save frame to SD if recording
    if (recording && fb) {
      saveFrameToSD(fb);
    }
    
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if(res != ESP_OK){
      break;
    }
    // Limit the frame rate
    delay(1000 / frameRate);
  }
  
  return res;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
  char*  buf;
  size_t buf_len;
  char variable[32] = {0,};
  char value[32] = {0,};
  
  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if(!buf){
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
          httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  
  int val = atoi(value);
  sensor_t * s = esp_camera_sensor_get();
  int res = 0;
  
  if(!strcmp(variable, "framesize")) {
    if(s->pixformat == PIXFORMAT_JPEG) {
      frameSize = (framesize_t)val;
      res = s->set_framesize(s, (framesize_t)val);
    }
  }
  else if(!strcmp(variable, "quality")) res = s->set_quality(s, val);
  else if(!strcmp(variable, "contrast")) res = s->set_contrast(s, val);
  else if(!strcmp(variable, "brightness")) res = s->set_brightness(s, val);
  else if(!strcmp(variable, "saturation")) res = s->set_saturation(s, val);
  else if(!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)val);
  else if(!strcmp(variable, "colorbar")) res = s->set_colorbar(s, val);
  else if(!strcmp(variable, "awb")) res = s->set_whitebal(s, val);
  else if(!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, val);
  else if(!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, val);
  else if(!strcmp(variable, "hmirror")) res = s->set_hmirror(s, val);
  else if(!strcmp(variable, "vflip")) res = s->set_vflip(s, val);
  else if(!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, val);
  else if(!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, val);
  else if(!strcmp(variable, "aec_value")) res = s->set_aec_value(s, val);
  else if(!strcmp(variable, "aec2")) res = s->set_aec2(s, val);
  else if(!strcmp(variable, "dcw")) res = s->set_dcw(s, val);
  else if(!strcmp(variable, "bpc")) res = s->set_bpc(s, val);
  else if(!strcmp(variable, "wpc")) res = s->set_wpc(s, val);
  else if(!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, val);
  else if(!strcmp(variable, "lenc")) res = s->set_lenc(s, val);
  else if(!strcmp(variable, "special_effect")) res = s->set_special_effect(s, val);
  else if(!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, val);
  else if(!strcmp(variable, "ae_level")) res = s->set_ae_level(s, val);
  else if(!strcmp(variable, "flash")) {
    flashState = val;
    digitalWrite(FLASH_GPIO_NUM, val);
  }
  else if(!strcmp(variable, "framerate")) {
    frameRate = val;
  }
  else if(!strcmp(variable, "record")) {
    toggleRecording();
  }
  else {
    res = -1;
  }
  
  if(res){
    return httpd_resp_send_500(req);
  }
  
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];
  
  sensor_t * s = esp_camera_sensor_get();
  char * p = json_response;
  *p++ = '{';
  
  p+=sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p+=sprintf(p, "\"quality\":%u,", s->status.quality);
  p+=sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p+=sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p+=sprintf(p, "\"saturation\":%d,", s->status.saturation);
  p+=sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
  p+=sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
  p+=sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
  p+=sprintf(p, "\"awb\":%u,", s->status.awb);
  p+=sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
  p+=sprintf(p, "\"aec\":%u,", s->status.aec);
  p+=sprintf(p, "\"aec2\":%u,", s->status.aec2);
  p+=sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
  p+=sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
  p+=sprintf(p, "\"agc\":%u,", s->status.agc);
  p+=sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
  p+=sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
  p+=sprintf(p, "\"bpc\":%u,", s->status.bpc);
  p+=sprintf(p, "\"wpc\":%u,", s->status.wpc);
  p+=sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
  p+=sprintf(p, "\"lenc\":%u,", s->status.lenc);
  p+=sprintf(p, "\"vflip\":%u,", s->status.vflip);
  p+=sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p+=sprintf(p, "\"dcw\":%u,", s->status.dcw);
  p+=sprintf(p, "\"colorbar\":%u,", s->status.colorbar);
  p+=sprintf(p, "\"flash\":%u,", flashState);
  p+=sprintf(p, "\"framerate\":%d,", frameRate);
  p+=sprintf(p, "\"recording\":%u", recording);
  *p++ = '}';
  *p++ = 0;
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  httpd_uri_t cmd_uri = {
    .uri = "/cmd",
    .method = HTTP_GET,
    .handler = cmd_handler,
    .user_ctx = NULL
  };

  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
  };

  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
  };

  Serial.printf("Starting web server on port: '%d'\n", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
  }

  config.server_port += 1;
  config.ctrl_port += 1;
  Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>ESP32-CAM</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: #f1f1f1;
            margin: 0;
            padding: 20px;
            color: #333;
        }
        .container {
            max-width: 1000px;
            margin: 0 auto;
            background: #fff;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        h1 {
            text-align: center;
            color: #444;
        }
        .video-container {
            position: relative;
            padding-bottom: 56.25%;
            height: 0;
            overflow: hidden;
            margin-bottom: 20px;
            background: #000;
            border-radius: 4px;
        }
        .video-container img {
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            object-fit: contain;
        }
        .controls {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin-bottom: 20px;
        }
        .control-group {
            flex: 1;
            min-width: 200px;
            background: #f9f9f9;
            padding: 15px;
            border-radius: 4px;
            box-shadow: 0 0 5px rgba(0,0,0,0.05);
        }
        .control-group h3 {
            margin-top: 0;
            color: #555;
            border-bottom: 1px solid #ddd;
            padding-bottom: 8px;
        }
        button {
            background: #4CAF50;
            color: white;
            border: none;
            padding: 10px 15px;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            font-size: 16px;
            margin: 4px 2px;
            cursor: pointer;
            border-radius: 4px;
            transition: background 0.3s;
        }
        button:hover {
            background: #45a049;
        }
        button.flash {
            background: #f39c12;
        }
        button.flash:hover {
            background: #e67e22;
        }
        button.record {
            background: #e74c3c;
        }
        button.record:hover {
            background: #c0392b;
        }
        .recording {
            animation: pulse 1s infinite;
        }
        @keyframes pulse {
            0% { opacity: 1; }
            50% { opacity: 0.5; }
            100% { opacity: 1; }
        }
        select, input {
            width: 100%;
            padding: 8px;
            margin: 5px 0 15px 0;
            display: inline-block;
            border: 1px solid #ccc;
            border-radius: 4px;
            box-sizing: border-box;
        }
        label {
            font-weight: bold;
            display: block;
            margin-bottom: 5px;
        }
        .status {
            background: #f9f9f9;
            padding: 15px;
            border-radius: 4px;
            margin-top: 20px;
            font-family: monospace;
            white-space: pre-wrap;
            word-break: break-all;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP32-CAM Control Panel</h1>
        
        <div class="video-container">
            <img id="stream" src="">
        </div>
        
        <div class="controls">
            <div class="control-group">
                <h3>Camera Controls</h3>
                <button class="flash" onclick="toggleFlash()">Toggle Flash</button>
                <button id="recordBtn" class="record" onclick="toggleRecording()">Start Recording</button>
                
                <label for="framesize">Resolution:</label>
                <select id="framesize" onchange="updateSetting('framesize', this.value)">
                    <option value="10">UXGA (1600x1200)</option>
                    <option value="9">SXGA (1280x1024)</option>
                    <option value="8">XGA (1024x768)</option>
                    <option value="7">SVGA (800x600)</option>
                    <option value="6">VGA (640x480)</option>
                    <option value="5">CIF (400x296)</option>
                    <option value="4">QVGA (320x240)</option>
                    <option value="3">HQVGA (240x176)</option>
                    <option value="0">QQVGA (160x120)</option>
                </select>
                
                <label for="framerate">Frame Rate (fps):</label>
                <input type="number" id="framerate" min="1" max="60" value="10" onchange="updateSetting('framerate', this.value)">
            </div>
            
            <div class="control-group">
                <h3>Image Settings</h3>
                
                <label for="quality">Quality (0-63):</label>
                <input type="range" id="quality" min="0" max="63" value="10" onchange="updateSetting('quality', this.value)">
                
                <label for="brightness">Brightness (-2 to 2):</label>
                <input type="range" id="brightness" min="-2" max="2" value="0" onchange="updateSetting('brightness', this.value)">
                
                <label for="contrast">Contrast (-2 to 2):</label>
                <input type="range" id="contrast" min="-2" max="2" value="0" onchange="updateSetting('contrast', this.value)">
                
                <label for="saturation">Saturation (-2 to 2):</label>
                <input type="range" id="saturation" min="-2" max="2" value="0" onchange="updateSetting('saturation', this.value)">
            </div>
        </div>
        
        <div class="status" id="status">
            Loading status...
        </div>
    </div>
    
    <script>
        // Initialize stream
        window.onload = function() {
            document.getElementById('stream').src = 'http://' + window.location.hostname + ':81/stream';
            fetchStatus();
            setInterval(fetchStatus, 5000);
        };
        
        function toggleFlash() {
            fetch('/cmd?var=flash&val=' + (!document.querySelector('.flash').classList.contains('active') ? 1 : 0))
                .then(() => fetchStatus());
        }
        
        function toggleRecording() {
            fetch('/cmd?var=record&val=toggle')
                .then(() => fetchStatus());
        }
        
        function updateSetting(variable, value) {
            fetch('/cmd?var=' + variable + '&val=' + value)
                .then(() => fetchStatus());
        }
        
        function fetchStatus() {
            fetch('/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('status').innerText = JSON.stringify(data, null, 2);
                    
                    // Update UI elements
                    document.getElementById('framesize').value = data.framesize;
                    document.getElementById('framerate').value = data.framerate;
                    document.getElementById('quality').value = data.quality;
                    document.getElementById('brightness').value = data.brightness;
                    document.getElementById('contrast').value = data.contrast;
                    document.getElementById('saturation').value = data.saturation;
                    
                    // Update flash button
                    const flashBtn = document.querySelector('.flash');
                    if(data.flash) {
                        flashBtn.classList.add('active');
                        flashBtn.textContent = 'Flash ON';
                    } else {
                        flashBtn.classList.remove('active');
                        flashBtn.textContent = 'Flash OFF';
                    }
                    
                    // Update record button
                    const recordBtn = document.getElementById('recordBtn');
                    if(data.recording) {
                        recordBtn.textContent = 'Stop Recording';
                        recordBtn.classList.add('recording');
                    } else {
                        recordBtn.textContent = 'Start Recording';
                        recordBtn.classList.remove('recording');
                    }
                });
        }
    </script>
</body>
</html>
)rawliteral";
