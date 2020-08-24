/*
 * app_httpd.c
 *
 *  Created on: 22 de ago de 2020
 *      Author: ceanm
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_vfs.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "cJSON.h"
#include "sdkconfig.h"

#include "app_common.h"
#include "app_httpd.h"
#include "app_camera.h"

#ifdef CONFIG_IDF_TARGET_ESP32
#define CHIP_NAME "ESP32"
#endif

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

static httpd_handle_t stream_httpd = NULL;
static httpd_handle_t camera_httpd = NULL;

//static int64_t last_frame = 0;

static esp_err_t system_info_handler(httpd_req_t *req);
static esp_err_t cam_status_handler(httpd_req_t *req);
static esp_err_t cam_stream_handler(httpd_req_t *req);
static esp_err_t cam_capture_handler(httpd_req_t *req);
static esp_err_t cam_cmd_handler(httpd_req_t *req);
static esp_err_t cam_xclk_handler(httpd_req_t *req);
static esp_err_t cam_reg_handler(httpd_req_t *req);
static esp_err_t cam_greg_handler(httpd_req_t *req);

typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

esp_err_t init_server(void) {
	rest_server_context_t *rest_context = NULL;
	rest_context = calloc(1, sizeof(rest_server_context_t));
	APP_ERROR_CHECK(!!rest_context, "No memory for rest context", err_init);

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.max_uri_handlers = 10;

	config.uri_match_fn = httpd_uri_match_wildcard;

	ESP_LOGI(APP_HTTPD_TAG, "Starting HTTP Server");
	APP_ERROR_CHECK(httpd_start(&camera_httpd, &config) == ESP_OK, "Start web server failed", err_init);

	/* URI handler for fetching system info */
	httpd_uri_t system_info_uri = {
		.uri = "/api/v1/system/info",
		.method = HTTP_GET,
		.handler = system_info_handler,
		.user_ctx = NULL
	};

	httpd_uri_t cam_status_uri = {
		.uri = "/api/v1/cam/status",
		.method = HTTP_GET,
		.handler = cam_status_handler,
		.user_ctx = NULL
	};

	httpd_uri_t cam_capture_uri = {
		.uri = "/api/v1/cam/capture",
		.method = HTTP_GET,
		.handler = cam_capture_handler,
		.user_ctx = NULL
	};

	httpd_uri_t cam_cmd_uri = {
		.uri = "/api/v1/cam/control",
		.method = HTTP_POST,
		.handler = cam_cmd_handler,
		.user_ctx = rest_context
	};

	httpd_uri_t cam_xclk_uri = {
		.uri = "/api/v1/cam/xclk",
		.method = HTTP_POST,
		.handler = cam_xclk_handler,
		.user_ctx = rest_context
	};

	httpd_uri_t cam_reg_uri = {
		.uri = "/api/v1/cam/reg",
		.method = HTTP_POST,
		.handler = cam_reg_handler,
		.user_ctx = rest_context
	};

	httpd_uri_t cam_greg_uri = {
		.uri = "/api/v1/cam/greg",
		.method = HTTP_POST,
		.handler = cam_greg_handler,
		.user_ctx = rest_context
	};

	httpd_register_uri_handler(camera_httpd, &system_info_uri);
	httpd_register_uri_handler(camera_httpd, &cam_status_uri);
	httpd_register_uri_handler(camera_httpd, &cam_capture_uri);
	httpd_register_uri_handler(camera_httpd, &cam_cmd_uri);
	httpd_register_uri_handler(camera_httpd, &cam_xclk_uri);
	httpd_register_uri_handler(camera_httpd, &cam_reg_uri);
	httpd_register_uri_handler(camera_httpd, &cam_greg_uri);

	config.server_port += 1;
	config.ctrl_port += 1;
	APP_ERROR_CHECK(httpd_start(&stream_httpd, &config) == ESP_OK, "Start stream server failed", err_init);

	httpd_uri_t cam_stream_uri = {
		.uri = "/api/v1/cam/stream",
		.method = HTTP_GET,
		.handler = cam_stream_handler,
		.user_ctx = NULL
	};

	httpd_register_uri_handler(stream_httpd, &cam_stream_uri);

	return ESP_OK;
err_init:
	if(!!rest_context) free(rest_context);

	return ESP_FAIL;
}

static void clear_get_json(cJSON *root, char *sys_info) {
	if(!!sys_info) free(sys_info);
	if(!!root) cJSON_Delete(root);
}

static esp_err_t send_json_error(httpd_req_t *req, cJSON *root, char *sys_info) {
	clear_get_json(root, sys_info);
	return httpd_resp_send_500(req);
}

static void get_chip_info(esp_chip_info_t *chip_info, cJSON *root) {
    cJSON *chip = cJSON_CreateObject();
    cJSON_AddStringToObject(chip, "name", CHIP_NAME);
    cJSON_AddNumberToObject(chip, "cores", chip_info->cores);

    char* features = malloc(11);
    strcpy(features, "WiFi");

    if(chip_info->features & CHIP_FEATURE_BT)
        strcat(features, "/BT");

    if(chip_info->features & CHIP_FEATURE_BLE)
        strcat(features, "/BLE");

    cJSON_AddStringToObject(chip, "features", features);
    free(features);

    cJSON_AddNumberToObject(chip, "revision", chip_info->revision);

    cJSON_AddItemToObject(root, "chip", chip);
}

static void get_flash_info(esp_chip_info_t *chip_info, cJSON *root) {
    cJSON *flash = cJSON_CreateObject();

    char flash_size[7];
    sprintf(flash_size, "%dMB", spi_flash_get_chip_size() / (1024 * 1024));
    cJSON_AddStringToObject(flash, "size", flash_size);

    cJSON_AddStringToObject(flash, "type", (chip_info->features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    cJSON_AddItemToObject(root, "flash", flash);
}

static esp_err_t system_info_handler(httpd_req_t *req) {
	esp_err_t res;
	httpd_resp_set_type(req, "application/json");
	cJSON *root = cJSON_CreateObject();
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);

	get_chip_info(&chip_info, root);
	get_flash_info(&chip_info, root);

	char *sys_info = cJSON_Print(root);
	APP_ERROR_CHECK((res = httpd_resp_sendstr(req, sys_info)) == ESP_OK, "Something wrong", err_info_with_resp);

	clear_get_json(root, sys_info);
	return res;
err_info_with_resp:
	res = send_json_error(req, root, sys_info);
	return res;
}

static esp_err_t cam_status_handler(httpd_req_t *req) {
	esp_err_t res;
	sensor_t *s = esp_camera_sensor_get();

	httpd_resp_set_type(req, "application/json");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	cJSON *root = cJSON_CreateObject();

	cJSON_AddStringToObject(root, "board", CAM_BOARD);
	cJSON_AddNumberToObject(root, "xclk", s->xclk_freq_hz / 1000000);
	cJSON_AddNumberToObject(root, "pixformat", s->pixformat);
	cJSON_AddNumberToObject(root, "framesize", s->status.framesize);
	cJSON_AddNumberToObject(root, "quality", s->status.quality);
	cJSON_AddNumberToObject(root, "brightness", s->status.brightness);
	cJSON_AddNumberToObject(root, "contrast", s->status.contrast);
	cJSON_AddNumberToObject(root, "saturation", s->status.saturation);
	cJSON_AddNumberToObject(root, "sharpness", s->status.sharpness);
	cJSON_AddNumberToObject(root, "special_effect", s->status.special_effect);
	cJSON_AddNumberToObject(root, "wb_mode", s->status.wb_mode);
	cJSON_AddNumberToObject(root, "awb", s->status.awb);
	cJSON_AddNumberToObject(root, "awb_gain", s->status.awb_gain);
	cJSON_AddNumberToObject(root, "aec", s->status.aec);
	cJSON_AddNumberToObject(root, "aec2", s->status.aec2);
	cJSON_AddNumberToObject(root, "ae_level", s->status.ae_level);
	cJSON_AddNumberToObject(root, "aec_value", s->status.aec_value);
	cJSON_AddNumberToObject(root, "agc", s->status.agc);
	cJSON_AddNumberToObject(root, "agc_gain", s->status.agc_gain);
	cJSON_AddNumberToObject(root, "gainceiling", s->status.gainceiling);
	cJSON_AddNumberToObject(root, "bpc", s->status.bpc);
	cJSON_AddNumberToObject(root, "wpc", s->status.wpc);
	cJSON_AddNumberToObject(root, "raw_gma", s->status.raw_gma);
	cJSON_AddNumberToObject(root, "lenc", s->status.lenc);
	cJSON_AddNumberToObject(root, "hmirror", s->status.hmirror);
	cJSON_AddNumberToObject(root, "dcw", s->status.dcw);
	cJSON_AddNumberToObject(root, "colorbar", s->status.colorbar);
	cJSON_AddNumberToObject(root, "led_intensity", -1);

	char *sys_info = cJSON_Print(root);
	APP_ERROR_CHECK((res = httpd_resp_sendstr(req, sys_info)) == ESP_OK, "Something wrong", err_status_with_resp);

	clear_get_json(root, sys_info);
	return res;
err_status_with_resp:
	res = send_json_error(req, root, sys_info);
	return res;
}

static esp_err_t cam_stream_handler(httpd_req_t *req) {
	esp_err_t res;
	camera_fb_t *fb = NULL;
	struct timeval _timestamp;

	size_t _jpg_buf_len = 0;
	uint8_t *_jpg_buf = NULL;
	char *part_buf[128];

//	if (!last_frame)
//		last_frame = esp_timer_get_time();

	APP_ERROR_CHECK(httpd_resp_set_type(req, _STREAM_CONTENT_TYPE) == ESP_OK, "Stream content type invalid", err_stream_with_resp);

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "X-Framerate", "60");

	while (true) {
		fb = esp_camera_fb_get();
		APP_ERROR_CHECK(!!fb, "Camera capture failed", err_stream_with_resp);

		_timestamp.tv_sec = fb->timestamp.tv_sec;
		_timestamp.tv_usec = fb->timestamp.tv_usec;
		if (fb->format != PIXFORMAT_JPEG) {
			bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
			APP_ERROR_CHECK(jpeg_converted, "JPEG compression failed", err_stream_with_resp);
		} else {
			_jpg_buf_len = fb->len;
			_jpg_buf = fb->buf;
		}

		APP_ERROR_CHECK((res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY))) == ESP_OK, "Error sending chunk", err_stream);

		size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
		APP_ERROR_CHECK((res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen)) == ESP_OK, "Error sending chunk (part buffer)", err_stream);

		APP_ERROR_CHECK((res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len)) == ESP_OK, "Error sending chunk (jpg buffer)", err_stream);

		_jpg_buf = NULL;
		esp_camera_fb_return(fb);
		fb = NULL;

//		int64_t fr_end = esp_timer_get_time();

//		int64_t frame_time = fr_end - last_frame;
//		last_frame = fr_end;
//		frame_time /= 1000;
//        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
//        ESP_LOGI(
//        	TAG,
//        	"MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)",
//             (uint32_t)(_jpg_buf_len),
//             (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
//             avg_frame_time, 1000.0 / avg_frame_time
//        );
	}

//	last_frame = 0;

	res = ESP_OK;
	return res;
err_stream_with_resp:
	res = httpd_resp_send_500(req);
err_stream:
	if (!!_jpg_buf) free(_jpg_buf);
	return res;
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if (!index)
        j->len = 0;

    if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
        return 0;

    j->len += len;
    return len;
}

static esp_err_t cam_capture_handler(httpd_req_t *req) {
	esp_err_t res;
	int64_t fr_start = esp_timer_get_time();

	camera_fb_t *fb = esp_camera_fb_get();

	APP_ERROR_CHECK(!!fb, "Camera capture failed", err_capture_with_resp);

	httpd_resp_set_type(req, "image/jpeg");
	httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	char ts[32];
	snprintf(ts, 32, "%ld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
	httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

	size_t fb_len = 0;
	if (fb->format == PIXFORMAT_JPEG) {
		fb_len = fb->len;
		APP_ERROR_CHECK((res = httpd_resp_send(req, (const char *)fb->buf, fb->len)) == ESP_OK, "Something wrong", err_capture);
	} else {
		jpg_chunking_t jchunk = {req, 0};
		APP_ERROR_CHECK(frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk), "Error when converting camera frame buffer to JPEG", err_capture_with_resp);
		res = httpd_resp_send_chunk(req, NULL, 0);
		fb_len = jchunk.len;
	}
	esp_camera_fb_return(fb);

	int64_t fr_end = esp_timer_get_time();
	ESP_LOGI(APP_HTTPD_TAG, "JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));

	return res;
err_capture_with_resp:
	res = httpd_resp_send_500(req);
err_capture:
	return res;
}

static char* getBuffer(httpd_req_t *req, esp_err_t *res) {
	char *buffer = NULL;
	int total_len = req->content_len;

	if (total_len >= SCRATCH_BUFSIZE) {
		/* Respond with 500 Internal Server Error */
		*res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
		APP_ERROR_CHECK(false, "", err_set_buffer);
	}

	int cur_len = 0;
	buffer = ((rest_server_context_t *)(req->user_ctx))->scratch;

	int received = 0;

	while (cur_len < total_len) {
		received = httpd_req_recv(req, buffer + cur_len, total_len);
		if (received <= 0) {
			/* Respond with 500 Internal Server Error */
			*res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to post control value");
			APP_ERROR_CHECK(false, "", err_set_buffer);
		}
		cur_len += received;
	}
	buffer[total_len] = '\0';

	return buffer;
err_set_buffer:
	return NULL;
}

static esp_err_t cam_cmd_handler(httpd_req_t *req) {
	esp_err_t res;
	cJSON *root = NULL;
	char *buf;

	APP_ERROR_CHECK(!!(buf = getBuffer(req, &res)), "An error occurred while loading the buffer", err_cmd);

	root = cJSON_Parse(buf);
	sensor_t *sensor = esp_camera_sensor_get();

	int framesize = JSON_GET_INT(root, "framesize");
	bool isValidFramesize = false;

	if (framesize != JSON_INT_ATTR_NOTFOUND && sensor->pixformat == PIXFORMAT_JPEG) {
		if(framesize < MIN_FRAMESIZE || framesize > MAX_FRAMESIZE) {
			char message[43];
			sprintf(message, "framesize value should be between %d and %d", MIN_FRAMESIZE, MAX_FRAMESIZE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidFramesize = true;
	}

	int quality = JSON_GET_INT(root, "quality");
	bool isValidQuality = false;

	if (quality != JSON_INT_ATTR_NOTFOUND) {
		if(quality < MIN_QUALITY || quality > MAX_QUALITY) {
			char message[41];
			sprintf(message, "quality value should be between %d and %d", MIN_QUALITY, MAX_QUALITY);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidQuality = true;
	}

	int contrast = JSON_GET_INT(root, "contrast");
	bool isValidContrast = false;

	if (contrast != JSON_INT_ATTR_NOTFOUND) {
		if(contrast < MIN_CONTRAST || contrast > MAX_CONTRAST) {
			char message[42];
			sprintf(message, "contrast value should be between %d and %d", MIN_CONTRAST, MAX_CONTRAST);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidContrast = true;
	}

	int brightness = JSON_GET_INT(root, "brightness");
	bool isValidBrightness = false;

	if (brightness != JSON_INT_ATTR_NOTFOUND) {
		if(brightness < MIN_BRIGHTNESS || brightness > MAX_BRIGHTNESS) {
			char message[44];
			sprintf(message, "brightness value should be between %d and %d", MIN_BRIGHTNESS, MAX_BRIGHTNESS);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidBrightness = true;
	}

	int saturation = JSON_GET_INT(root, "saturation");
	bool isValidSaturation = false;

	if (saturation != JSON_INT_ATTR_NOTFOUND) {
		if(saturation < MIN_SATURATION || saturation > MAX_SATURATION) {
			char message[44];
			sprintf(message, "saturation value should be between %d and %d", MIN_SATURATION, MAX_SATURATION);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidSaturation = true;
	}

	int gainceiling = JSON_GET_INT(root, "gainceiling");
	bool isValidGainceiling = false;

	if (gainceiling != JSON_INT_ATTR_NOTFOUND) {
		if(gainceiling < MIN_GAINCEILING || gainceiling > MAX_GAINCEILING) {
			char message[44];
			sprintf(message, "gainceiling value should be between %d and %d", MIN_GAINCEILING, MAX_GAINCEILING);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidGainceiling = true;
	}

	int colorbar = JSON_GET_INT(root, "colorbar");
	bool isValidColorbar = false;

	if (colorbar != JSON_INT_ATTR_NOTFOUND) {
		if(colorbar != JSON_ATTR_FALSE && colorbar != JSON_ATTR_TRUE) {
			char message[30];
			sprintf(message, "colorbar value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidColorbar = true;
	}

	int awb = JSON_GET_INT(root, "awb");
	bool isValidAwb = false;

	if (awb != JSON_INT_ATTR_NOTFOUND) {
		if(awb != JSON_ATTR_FALSE && awb != JSON_ATTR_TRUE) {
			char message[25];
			sprintf(message, "awb value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidAwb = true;
	}

	int agc = JSON_GET_INT(root, "agc");
	bool isValidAgc = false;

	if (agc != JSON_INT_ATTR_NOTFOUND) {
		if(agc != JSON_ATTR_FALSE && agc != JSON_ATTR_TRUE) {
			char message[25];
			sprintf(message, "agc value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidAgc = true;
	}

	int aec = JSON_GET_INT(root, "aec");
	bool isValidAec = false;

	if (aec != JSON_INT_ATTR_NOTFOUND) {
		if(aec != JSON_ATTR_FALSE && aec != JSON_ATTR_TRUE) {
			char message[25];
			sprintf(message, "aec value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidAec = true;
	}

	int hmirror = JSON_GET_INT(root, "hmirror");
	bool isValidHMirror = false;

	if (hmirror != JSON_INT_ATTR_NOTFOUND) {
		if(hmirror != JSON_ATTR_FALSE && hmirror != JSON_ATTR_TRUE) {
			char message[29];
			sprintf(message, "hmirror value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidHMirror = true;
	}

	int vflip = JSON_GET_INT(root, "vflip");
	bool isValidVFlip = false;

	if (vflip != JSON_INT_ATTR_NOTFOUND) {
		if(vflip != JSON_ATTR_FALSE && vflip != JSON_ATTR_TRUE) {
			char message[27];
			sprintf(message, "vflip value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidVFlip = true;
	}

	int awb_gain = JSON_GET_INT(root, "awb_gain");
	bool isValidAwbGain = false;

	if (awb_gain != JSON_INT_ATTR_NOTFOUND) {
		if(awb_gain != JSON_ATTR_FALSE && awb_gain != JSON_ATTR_TRUE) {
			char message[30];
			sprintf(message, "awb_gain value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidAwbGain = true;
	}

	int agc_gain = JSON_GET_INT(root, "agc_gain");
	bool isValidAgcGain = false;

	if (agc_gain != JSON_INT_ATTR_NOTFOUND) {
		if(agc_gain < MIN_AGC_GAIN || agc_gain > MAX_AGC_GAIN) {
			char message[42];
			sprintf(message, "agc_gain value should be between %d and %d", MIN_AGC_GAIN, MAX_AGC_GAIN);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidAgcGain = true;
	}

	int aec_value = JSON_GET_INT(root, "aec_value");
	bool isValidAecValue = false;

	if (aec_value != JSON_INT_ATTR_NOTFOUND) {
		if(aec_value < MIN_AEC_VALUE || aec_value > MAX_AEC_VALUE) {
			char message[45];
			sprintf(message, "aec_value value should be between %d and %d", MIN_AEC_VALUE, MAX_AEC_VALUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidAecValue = true;
	}

	int aec2 = JSON_GET_INT(root, "aec2");
	bool isValidAec2 = false;

	if (aec2 != JSON_INT_ATTR_NOTFOUND) {
		if(aec2 != JSON_ATTR_FALSE && aec2 != JSON_ATTR_TRUE) {
			char message[26];
			sprintf(message, "aec2 value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidAec2 = true;
	}

	int dcw = JSON_GET_INT(root, "dcw");
	bool isValidDcw = false;

	if (dcw != JSON_INT_ATTR_NOTFOUND) {
		if(dcw != JSON_ATTR_FALSE && dcw != JSON_ATTR_TRUE) {
			char message[25];
			sprintf(message, "dcw value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidDcw = true;
	}

	int bpc = JSON_GET_INT(root, "bpc");
	bool isValidBpc = false;

	if (bpc != JSON_INT_ATTR_NOTFOUND) {
		if(bpc != JSON_ATTR_FALSE && bpc != JSON_ATTR_TRUE) {
			char message[25];
			sprintf(message, "bpc value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidBpc = true;
	}

	int wpc = JSON_GET_INT(root, "wpc");
	bool isValidWpc = false;

	if (wpc != JSON_INT_ATTR_NOTFOUND) {
		if(wpc != JSON_ATTR_FALSE && wpc != JSON_ATTR_TRUE) {
			char message[25];
			sprintf(message, "wpc value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidWpc = true;
	}

	int raw_gma = JSON_GET_INT(root, "raw_gma");
	bool isValidRawGma = false;

	if (raw_gma != JSON_INT_ATTR_NOTFOUND) {
		if(raw_gma != JSON_ATTR_FALSE && raw_gma != JSON_ATTR_TRUE) {
			char message[29];
			sprintf(message, "raw_gma value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidRawGma = true;
	}

	int lenc = JSON_GET_INT(root, "lenc");
	bool isValidLenc = false;

	if (lenc != JSON_INT_ATTR_NOTFOUND) {
		if(lenc != JSON_ATTR_FALSE && lenc != JSON_ATTR_TRUE) {
			char message[26];
			sprintf(message, "lenc value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidLenc = true;
	}

	int special_effect = JSON_GET_INT(root, "special_effect");
	bool isValidSpecialEffect = false;

	if (special_effect != JSON_INT_ATTR_NOTFOUND) {
		if(special_effect < MIN_SPECIAL_EFFECT || special_effect > MAX_SPECIAL_EFFECT) {
			char message[47];
			sprintf(message, "special_effect value should be between %d and %d", MIN_SPECIAL_EFFECT, MAX_SPECIAL_EFFECT);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidSpecialEffect = true;
	}

	int wb_mode = JSON_GET_INT(root, "wb_mode");
	bool isValidWbMode = false;

	if (wb_mode != JSON_INT_ATTR_NOTFOUND) {
		if(wb_mode < MIN_WB_MODE || wb_mode > MAX_WB_MODE) {
			char message[40];
			sprintf(message, "wb_mode value should be between %d and %d", MIN_WB_MODE, MAX_WB_MODE);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidWbMode = true;
	}

	int ae_level = JSON_GET_INT(root, "ae_level");
	bool isValidAeLevel = false;

	if (ae_level != JSON_INT_ATTR_NOTFOUND) {
		if(ae_level < MIN_AE_LEVEL || ae_level > MAX_AE_LEVEL) {
			char message[42];
			sprintf(message, "ae_level value should be between %d and %d", MIN_AE_LEVEL, MAX_AE_LEVEL);
			res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
			APP_ERROR_CHECK(false, "", err_cmd);
		}
		isValidAeLevel = true;
	}


	//Resolution
	if(isValidFramesize && !!sensor->set_framesize(sensor, (framesize_t)framesize)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change framesize");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Quality
	if(isValidQuality && !!sensor->set_quality(sensor, quality)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change quality");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Contrast
	if(isValidContrast && !!sensor->set_contrast(sensor, contrast)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change contrast");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Brightness
	if(isValidBrightness && !!sensor->set_brightness(sensor, brightness)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change brightness");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Saturation
	if(isValidSaturation && !!sensor->set_saturation(sensor, saturation)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change saturation");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Gain Ceiling
	if(isValidGainceiling && !!sensor->set_gainceiling(sensor, (gainceiling_t)gainceiling)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change gainceiling");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Color Bar
	if(isValidColorbar && !!sensor->set_colorbar(sensor, colorbar)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change colorbar");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//AWB
	if(isValidAwb && !!sensor->set_whitebal(sensor, awb)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change awb");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//AGC
	if(isValidAgc && !!sensor->set_gain_ctrl(sensor, agc)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change agc");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//AEC SENSOR
	if(isValidAec && !!sensor->set_exposure_ctrl(sensor, aec)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change aec");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//H-Mirror
	if(isValidHMirror && !!sensor->set_hmirror(sensor, hmirror)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change hmirror");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//V-Flip
	if(isValidVFlip && !!sensor->set_vflip(sensor, vflip)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change vflip");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//AWB Gain
	if(isValidAwbGain && !!sensor->set_awb_gain(sensor, awb_gain)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change awb_gain");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Gain
	if(isValidAgcGain && !!sensor->set_agc_gain(sensor, agc_gain)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change agc_gain");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Exposure
	if(isValidAecValue && !!sensor->set_aec_value(sensor, aec_value)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change aec_value");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//AEC DSP
	if(isValidAec2 && !!sensor->set_aec2(sensor, aec2)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change aec2");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//DCW (Downsize EN)
	if(isValidDcw && !!sensor->set_dcw(sensor, dcw)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change dcw");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//BPC
	if(isValidBpc && !!sensor->set_bpc(sensor, bpc)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change bpc");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//WPC
	if(isValidWpc && !!sensor->set_wpc(sensor, wpc)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change wpc");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Raw GMA
	if(isValidRawGma && !!sensor->set_raw_gma(sensor, raw_gma)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change raw_gma");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Lens Correction
	if(isValidLenc && !!sensor->set_lenc(sensor, lenc)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change lenc");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Special Effect
	if(isValidSpecialEffect && !!sensor->set_special_effect(sensor, special_effect)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change special_effect");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//WB Mode
	if(isValidWbMode && !!sensor->set_wb_mode(sensor, wb_mode)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change wb_mode");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//AE Level
	if(isValidAeLevel && !!sensor->set_ae_level(sensor, ae_level)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change ae_level");
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	cJSON_Delete(root);

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	res = httpd_resp_send(req, NULL, 0);

	ESP_LOGI(APP_HTTPD_TAG,
		"Set Control:\n\tframesize = %d\n\tquality = %d\n\tcontrast = %d\n\tbrightness = %d\n\tsaturation = %d\n\tgainceiling = %d\n\tcolorbar = %d\n\tawb = %d\n\tagc = %d\n\taec = %d\n\thmirror = %d\n\tvflip = %d\n\tawb_gain = %d\n\tagc_gain = %d\n\taec_value = %d\n\taec2 = %d\n\tdcw = %d\n\tbpc = %d\n\twpc = %d\n\traw_gma = %d\n\tlenc = %d\n\tspecial_effect = %d\n\twb_mode = %d\n\tae_level = %d",
		framesize,
		quality,
		contrast,
		brightness,
		saturation,
		gainceiling,
		colorbar,
		awb,
		agc,
		aec,
		hmirror,
		vflip,
		awb_gain,
		agc_gain,
		aec_value,
		aec2,
		dcw,
		bpc,
		wpc,
		raw_gma,
		lenc,
		special_effect,
		wb_mode,
		ae_level
	);
	return res;
err_cmd:
	if(!!root) cJSON_Delete(root);
	return res;
}

static esp_err_t cam_xclk_handler(httpd_req_t *req) {
	esp_err_t res;
	cJSON *root = NULL;
	char *buf;

	APP_ERROR_CHECK(!!(buf = getBuffer(req, &res)), "An error occurred while loading the buffer", err_xclk);

	root = cJSON_Parse(buf);
	sensor_t *sensor = esp_camera_sensor_get();

	int xclk = JSON_GET_INT(root, "xclk");

	if (xclk == JSON_INT_ATTR_NOTFOUND) {
		res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content");
		APP_ERROR_CHECK(false, "", err_xclk);
	}

	if(xclk < MIN_XCLK_MHZ || xclk > MAX_XCLK_MHZ) {
		char message[38];
		sprintf(message, "xclk value should be between %d and %d", MIN_XCLK_MHZ, MAX_XCLK_MHZ);
		res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
		APP_ERROR_CHECK(false, "", err_xclk);
	}

	if(!!sensor->set_xclk(sensor, LEDC_TIMER_0, xclk)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change xclk");
		APP_ERROR_CHECK(false, "", err_xclk);
	}

	cJSON_Delete(root);

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	res = httpd_resp_send(req, NULL, 0);

	ESP_LOGI(APP_HTTPD_TAG, "Set XCLK: %d MHz", xclk);
	return res;
err_xclk:
	if(!!root) cJSON_Delete(root);
	return res;
}

static esp_err_t cam_reg_handler(httpd_req_t *req) {
	esp_err_t res;
	cJSON *root = NULL;
	char *buf;

	APP_ERROR_CHECK(!!(buf = getBuffer(req, &res)), "An error occurred while loading the buffer", err_reg);

	root = cJSON_Parse(buf);
	sensor_t *sensor = esp_camera_sensor_get();

	int reg = JSON_GET_INT(root, "reg");
	int mask = JSON_GET_INT(root, "mask");
	int val = JSON_GET_INT(root, "val");

	if (reg == JSON_INT_ATTR_NOTFOUND || mask == JSON_INT_ATTR_NOTFOUND || val == JSON_INT_ATTR_NOTFOUND) {
		res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content");
		APP_ERROR_CHECK(false, "", err_reg);
	}

	if(!!sensor->set_reg(sensor, reg, mask, val)) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to change reg, mask or val");
		APP_ERROR_CHECK(false, "", err_reg);
	}

	cJSON_Delete(root);

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	res = httpd_resp_send(req, NULL, 0);

	ESP_LOGI(APP_HTTPD_TAG, "Set Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);
	return res;
err_reg:
	if(!!root) cJSON_Delete(root);
	return res;
}

static esp_err_t cam_greg_handler(httpd_req_t *req) {
	esp_err_t res;
	cJSON *root = NULL;
	char *buf;

	APP_ERROR_CHECK(!!(buf = getBuffer(req, &res)), "An error occurred while loading the buffer", err_greg);

	root = cJSON_Parse(buf);
	sensor_t *sensor = esp_camera_sensor_get();

	int reg = JSON_GET_INT(root, "reg");
	int mask = JSON_GET_INT(root, "mask");
	int val;

	if (reg == JSON_INT_ATTR_NOTFOUND || mask == JSON_INT_ATTR_NOTFOUND) {
		res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content");
		APP_ERROR_CHECK(false, "", err_greg);
	}

	if((val = sensor->get_reg(sensor, reg, mask)) < 0) {
		res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An error occurred when try to get value");
		APP_ERROR_CHECK(false, "", err_greg);
	}

	cJSON_Delete(root);

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	char buf_val[20];
	const char * stringval = itoa(val, buf_val, 10);
	res = httpd_resp_send(req, stringval, strlen(stringval));

	ESP_LOGI(APP_HTTPD_TAG, "Get Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);
	return res;
err_greg:
	if(!!root) cJSON_Delete(root);
	return res;
}
