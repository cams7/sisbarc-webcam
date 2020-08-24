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
	httpd_resp_set_type(req, CONTENT_TYPE_APPLICATION_JSON);
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

	httpd_resp_set_type(req, CONTENT_TYPE_APPLICATION_JSON);
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

	httpd_resp_set_type(req, CONTENT_TYPE_IMAGE_JPEG);
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
		httpd_resp_set_type(req, CONTENT_TYPE_TEXT_PLAIN);
		httpd_resp_set_status(req, _400_BAD_REQUEST);
		*res = httpd_resp_sendstr(req, "Content too long");
		APP_ERROR_CHECK(false, "", err_set_buffer);
	}

	int cur_len = 0;
	buffer = ((rest_server_context_t *)(req->user_ctx))->scratch;

	int received = 0;

	while (cur_len < total_len) {
		received = httpd_req_recv(req, buffer + cur_len, total_len);
		if (received <= 0) {
			/* Respond with 500 Internal Server Error */
			httpd_resp_set_type(req, CONTENT_TYPE_TEXT_PLAIN);
			httpd_resp_set_status(req, _400_BAD_REQUEST);
			*res = httpd_resp_sendstr(req, "Failed to post control value");
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
	cJSON *req_content = NULL;
	cJSON *res_content = NULL;
	char *buf;

	APP_ERROR_CHECK(!!(buf = getBuffer(req, &res)), "An error occurred while loading the buffer", err_cmd);

	httpd_resp_set_type(req, CONTENT_TYPE_TEXT_PLAIN);

	req_content = cJSON_Parse(buf);
	sensor_t *sensor = esp_camera_sensor_get();
	
	bool hasError = false;
	res_content = cJSON_CreateObject();

	int framesize = JSON_GET_INT(req_content, "framesize");
	bool isValidFramesize = false;

	if (framesize != JSON_INT_ATTR_NOTFOUND && sensor->pixformat == PIXFORMAT_JPEG) {
		if(framesize < MIN_FRAMESIZE || framesize > MAX_FRAMESIZE) {
			char message[33];
			sprintf(message, "Value should be between %d and %d", MIN_FRAMESIZE, MAX_FRAMESIZE);
			cJSON_AddStringToObject(res_content, "framesize", message);
			hasError = true;
		}
		isValidFramesize = true;
	}

	int quality = JSON_GET_INT(req_content, "quality");
	bool isValidQuality = false;

	if (quality != JSON_INT_ATTR_NOTFOUND) {
		if(quality < MIN_QUALITY || quality > MAX_QUALITY) {
			char message[33];
			sprintf(message, "Value should be between %d and %d", MIN_QUALITY, MAX_QUALITY);
			cJSON_AddStringToObject(res_content, "quality", message);
			hasError = true;
		}
		isValidQuality = true;
	}

	int contrast = JSON_GET_INT(req_content, "contrast");
	bool isValidContrast = false;

	if (contrast != JSON_INT_ATTR_NOTFOUND) {
		if(contrast < MIN_CONTRAST || contrast > MAX_CONTRAST) {
			char message[33];
			sprintf(message, "Value should be between %d and %d", MIN_CONTRAST, MAX_CONTRAST);
			cJSON_AddStringToObject(res_content, "contrast", message);
			hasError = true;
		}
		isValidContrast = true;
	}

	int brightness = JSON_GET_INT(req_content, "brightness");
	bool isValidBrightness = false;

	if (brightness != JSON_INT_ATTR_NOTFOUND) {
		if(brightness < MIN_BRIGHTNESS || brightness > MAX_BRIGHTNESS) {
			char message[33];
			sprintf(message, "Value should be between %d and %d", MIN_BRIGHTNESS, MAX_BRIGHTNESS);
			cJSON_AddStringToObject(res_content, "brightness", message);
			hasError = true;
		}
		isValidBrightness = true;
	}

	int saturation = JSON_GET_INT(req_content, "saturation");
	bool isValidSaturation = false;

	if (saturation != JSON_INT_ATTR_NOTFOUND) {
		if(saturation < MIN_SATURATION || saturation > MAX_SATURATION) {
			char message[33];
			sprintf(message, "Value should be between %d and %d", MIN_SATURATION, MAX_SATURATION);
			cJSON_AddStringToObject(res_content, "saturation", message);
			hasError = true;
		}
		isValidSaturation = true;
	}

	int gainceiling = JSON_GET_INT(req_content, "gainceiling");
	bool isValidGainceiling = false;

	if (gainceiling != JSON_INT_ATTR_NOTFOUND) {
		if(gainceiling < MIN_GAINCEILING || gainceiling > MAX_GAINCEILING) {
			char message[33];
			sprintf(message, "Value should be between %d and %d", MIN_GAINCEILING, MAX_GAINCEILING);
			cJSON_AddStringToObject(res_content, "gainceiling", message);
			hasError = true;
		}
		isValidGainceiling = true;
	}

	int colorbar = JSON_GET_INT(req_content, "colorbar");
	bool isValidColorbar = false;

	if (colorbar != JSON_INT_ATTR_NOTFOUND) {
		if(colorbar != JSON_ATTR_FALSE && colorbar != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "colorbar", message);
			hasError = true;
		}
		isValidColorbar = true;
	}

	int awb = JSON_GET_INT(req_content, "awb");
	bool isValidAwb = false;

	if (awb != JSON_INT_ATTR_NOTFOUND) {
		if(awb != JSON_ATTR_FALSE && awb != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "awb", message);
			hasError = true;
		}
		isValidAwb = true;
	}

	int agc = JSON_GET_INT(req_content, "agc");
	bool isValidAgc = false;

	if (agc != JSON_INT_ATTR_NOTFOUND) {
		if(agc != JSON_ATTR_FALSE && agc != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "agc", message);
			hasError = true;
		}
		isValidAgc = true;
	}

	int aec = JSON_GET_INT(req_content, "aec");
	bool isValidAec = false;

	if (aec != JSON_INT_ATTR_NOTFOUND) {
		if(aec != JSON_ATTR_FALSE && aec != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "aec", message);
			hasError = true;
		}
		isValidAec = true;
	}

	int hmirror = JSON_GET_INT(req_content, "hmirror");
	bool isValidHMirror = false;

	if (hmirror != JSON_INT_ATTR_NOTFOUND) {
		if(hmirror != JSON_ATTR_FALSE && hmirror != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "hmirror", message);
			hasError = true;
		}
		isValidHMirror = true;
	}

	int vflip = JSON_GET_INT(req_content, "vflip");
	bool isValidVFlip = false;

	if (vflip != JSON_INT_ATTR_NOTFOUND) {
		if(vflip != JSON_ATTR_FALSE && vflip != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "vflip", message);
			hasError = true;
		}
		isValidVFlip = true;
	}

	int awb_gain = JSON_GET_INT(req_content, "awb_gain");
	bool isValidAwbGain = false;

	if (awb_gain != JSON_INT_ATTR_NOTFOUND) {
		if(awb_gain != JSON_ATTR_FALSE && awb_gain != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "awb_gain", message);
			hasError = true;
		}
		isValidAwbGain = true;
	}

	int agc_gain = JSON_GET_INT(req_content, "agc_gain");
	bool isValidAgcGain = false;

	if (agc_gain != JSON_INT_ATTR_NOTFOUND) {
		if(agc_gain < MIN_AGC_GAIN || agc_gain > MAX_AGC_GAIN) {
			char message[33];
			sprintf(message, "Value should be between %d and %d", MIN_AGC_GAIN, MAX_AGC_GAIN);
			cJSON_AddStringToObject(res_content, "agc_gain", message);
			hasError = true;
		}
		isValidAgcGain = true;
	}

	int aec_value = JSON_GET_INT(req_content, "aec_value");
	bool isValidAecValue = false;

	if (aec_value != JSON_INT_ATTR_NOTFOUND) {
		if(aec_value < MIN_AEC_VALUE || aec_value > MAX_AEC_VALUE) {
			char message[35];
			sprintf(message, "Value should be between %d and %d", MIN_AEC_VALUE, MAX_AEC_VALUE);
			cJSON_AddStringToObject(res_content, "aec_value", message);
			hasError = true;
		}
		isValidAecValue = true;
	}

	int aec2 = JSON_GET_INT(req_content, "aec2");
	bool isValidAec2 = false;

	if (aec2 != JSON_INT_ATTR_NOTFOUND) {
		if(aec2 != JSON_ATTR_FALSE && aec2 != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "aec2", message);
			hasError = true;
		}
		isValidAec2 = true;
	}

	int dcw = JSON_GET_INT(req_content, "dcw");
	bool isValidDcw = false;

	if (dcw != JSON_INT_ATTR_NOTFOUND) {
		if(dcw != JSON_ATTR_FALSE && dcw != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "dcw", message);
			hasError = true;
		}
		isValidDcw = true;
	}

	int bpc = JSON_GET_INT(req_content, "bpc");
	bool isValidBpc = false;

	if (bpc != JSON_INT_ATTR_NOTFOUND) {
		if(bpc != JSON_ATTR_FALSE && bpc != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "bpc", message);
			hasError = true;
		}
		isValidBpc = true;
	}

	int wpc = JSON_GET_INT(req_content, "wpc");
	bool isValidWpc = false;

	if (wpc != JSON_INT_ATTR_NOTFOUND) {
		if(wpc != JSON_ATTR_FALSE && wpc != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "wpc", message);
			hasError = true;
		}
		isValidWpc = true;
	}

	int raw_gma = JSON_GET_INT(req_content, "raw_gma");
	bool isValidRawGma = false;

	if (raw_gma != JSON_INT_ATTR_NOTFOUND) {
		if(raw_gma != JSON_ATTR_FALSE && raw_gma != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "raw_gma", message);
			hasError = true;
		}
		isValidRawGma = true;
	}

	int lenc = JSON_GET_INT(req_content, "lenc");
	bool isValidLenc = false;

	if (lenc != JSON_INT_ATTR_NOTFOUND) {
		if(lenc != JSON_ATTR_FALSE && lenc != JSON_ATTR_TRUE) {
			char message[21];
			sprintf(message, "Value must be %d or %d", JSON_ATTR_FALSE, JSON_ATTR_TRUE);
			cJSON_AddStringToObject(res_content, "lenc", message);
			hasError = true;
		}
		isValidLenc = true;
	}

	int special_effect = JSON_GET_INT(req_content, "special_effect");
	bool isValidSpecialEffect = false;

	if (special_effect != JSON_INT_ATTR_NOTFOUND) {
		if(special_effect < MIN_SPECIAL_EFFECT || special_effect > MAX_SPECIAL_EFFECT) {
			char message[33];
			sprintf(message, "Value should be between %d and %d", MIN_SPECIAL_EFFECT, MAX_SPECIAL_EFFECT);
			cJSON_AddStringToObject(res_content, "special_effect", message);
			hasError = true;
		}
		isValidSpecialEffect = true;
	}

	int wb_mode = JSON_GET_INT(req_content, "wb_mode");
	bool isValidWbMode = false;

	if (wb_mode != JSON_INT_ATTR_NOTFOUND) {
		if(wb_mode < MIN_WB_MODE || wb_mode > MAX_WB_MODE) {
			char message[33];
			sprintf(message, "Value should be between %d and %d", MIN_WB_MODE, MAX_WB_MODE);
			cJSON_AddStringToObject(res_content, "wb_mode", message);
			hasError = true;
		}
		isValidWbMode = true;
	}

	int ae_level = JSON_GET_INT(req_content, "ae_level");
	bool isValidAeLevel = false;

	if (ae_level != JSON_INT_ATTR_NOTFOUND) {
		if(ae_level < MIN_AE_LEVEL || ae_level > MAX_AE_LEVEL) {
			char message[33];
			sprintf(message, "Value should be between %d and %d", MIN_AE_LEVEL, MAX_AE_LEVEL);
			cJSON_AddStringToObject(res_content, "ae_level", message);
			hasError = true;
		}
		isValidAeLevel = true;
	}

	if(hasError) {
		char *message = cJSON_Print(res_content);
		httpd_resp_set_type(req, CONTENT_TYPE_APPLICATION_JSON);
		httpd_resp_set_status(req, _400_BAD_REQUEST);
		res = httpd_resp_sendstr(req, message);
		free(message);
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	//Resolution
	if(isValidFramesize && !!sensor->set_framesize(sensor, (framesize_t)framesize)) {
		cJSON_AddStringToObject(res_content, "framesize", "Something wrong");
		hasError = true;
	}

	//Quality
	if(isValidQuality && !!sensor->set_quality(sensor, quality)) {
		cJSON_AddStringToObject(res_content, "quality", "Something wrong");
		hasError = true;
	}

	//Contrast
	if(isValidContrast && !!sensor->set_contrast(sensor, contrast)) {
		cJSON_AddStringToObject(res_content, "contrast", "Something wrong");
		hasError = true;
	}

	//Brightness
	if(isValidBrightness && !!sensor->set_brightness(sensor, brightness)) {
		cJSON_AddStringToObject(res_content, "brightness", "Something wrong");
		hasError = true;
	}

	//Saturation
	if(isValidSaturation && !!sensor->set_saturation(sensor, saturation)) {
		cJSON_AddStringToObject(res_content, "saturation", "Something wrong");
		hasError = true;
	}

	//Gain Ceiling
	if(isValidGainceiling && !!sensor->set_gainceiling(sensor, (gainceiling_t)gainceiling)) {
		cJSON_AddStringToObject(res_content, "gainceiling", "Something wrong");
		hasError = true;
	}

	//Color Bar
	if(isValidColorbar && !!sensor->set_colorbar(sensor, colorbar)) {
		cJSON_AddStringToObject(res_content, "colorbar", "Something wrong");
		hasError = true;
	}

	//AWB
	if(isValidAwb && !!sensor->set_whitebal(sensor, awb)) {
		cJSON_AddStringToObject(res_content, "awb", "Something wrong");
		hasError = true;
	}

	//AGC
	if(isValidAgc && !!sensor->set_gain_ctrl(sensor, agc)) {
		cJSON_AddStringToObject(res_content, "agc", "Something wrong");
		hasError = true;
	}

	//AEC SENSOR
	if(isValidAec && !!sensor->set_exposure_ctrl(sensor, aec)) {
		cJSON_AddStringToObject(res_content, "aec", "Something wrong");
		hasError = true;
	}

	//H-Mirror
	if(isValidHMirror && !!sensor->set_hmirror(sensor, hmirror)) {
		cJSON_AddStringToObject(res_content, "hmirror", "Something wrong");
		hasError = true;
	}

	//V-Flip
	if(isValidVFlip && !!sensor->set_vflip(sensor, vflip)) {
		cJSON_AddStringToObject(res_content, "vflip", "Something wrong");
		hasError = true;
	}

	//AWB Gain
	if(isValidAwbGain && !!sensor->set_awb_gain(sensor, awb_gain)) {
		cJSON_AddStringToObject(res_content, "awb_gain", "Something wrong");
		hasError = true;
	}

	//Gain
	if(isValidAgcGain && !!sensor->set_agc_gain(sensor, agc_gain)) {
		cJSON_AddStringToObject(res_content, "agc_gain", "Something wrong");
		hasError = true;
	}

	//Exposure
	if(isValidAecValue && !!sensor->set_aec_value(sensor, aec_value)) {
		cJSON_AddStringToObject(res_content, "aec_value", "Something wrong");
		hasError = true;
	}

	//AEC DSP
	if(isValidAec2 && !!sensor->set_aec2(sensor, aec2)) {
		cJSON_AddStringToObject(res_content, "aec2", "Something wrong");
		hasError = true;
	}

	//DCW (Downsize EN)
	if(isValidDcw && !!sensor->set_dcw(sensor, dcw)) {
		cJSON_AddStringToObject(res_content, "dcw", "Something wrong");
		hasError = true;
	}

	//BPC
	if(isValidBpc && !!sensor->set_bpc(sensor, bpc)) {
		cJSON_AddStringToObject(res_content, "bpc", "Something wrong");
		hasError = true;
	}

	//WPC
	if(isValidWpc && !!sensor->set_wpc(sensor, wpc)) {
		cJSON_AddStringToObject(res_content, "wpc", "Something wrong");
		hasError = true;
	}

	//Raw GMA
	if(isValidRawGma && !!sensor->set_raw_gma(sensor, raw_gma)) {
		cJSON_AddStringToObject(res_content, "raw_gma", "Something wrong");
		hasError = true;
	}

	//Lens Correction
	if(isValidLenc && !!sensor->set_lenc(sensor, lenc)) {
		cJSON_AddStringToObject(res_content, "lenc", "Something wrong");
		hasError = true;
	}

	//Special Effect
	if(isValidSpecialEffect && !!sensor->set_special_effect(sensor, special_effect)) {
		cJSON_AddStringToObject(res_content, "special_effect", "Something wrong");
		hasError = true;
	}

	//WB Mode
	if(isValidWbMode && !!sensor->set_wb_mode(sensor, wb_mode)) {
		cJSON_AddStringToObject(res_content, "wb_mode", "Something wrong");
		hasError = true;
	}

	//AE Level
	if(isValidAeLevel && !!sensor->set_ae_level(sensor, ae_level)) {
		cJSON_AddStringToObject(res_content, "ae_level", "Something wrong");
		hasError = true;
	}

	if(hasError) {
		char *message = cJSON_Print(res_content);
		httpd_resp_set_type(req, CONTENT_TYPE_APPLICATION_JSON);
		httpd_resp_set_status(req, _500_INTERNAL_SERVER_ERROR);
		res = httpd_resp_sendstr(req, message);
		free(message);
		APP_ERROR_CHECK(false, "", err_cmd);
	}

	cJSON_Delete(req_content);
	cJSON_Delete(res_content);

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
	if(!!req_content) cJSON_Delete(req_content);
	if(!!res_content) cJSON_Delete(res_content);
	return res;
}

static esp_err_t cam_xclk_handler(httpd_req_t *req) {
	esp_err_t res;
	cJSON *req_content = NULL;
	char *buf;

	APP_ERROR_CHECK(!!(buf = getBuffer(req, &res)), "An error occurred while loading the buffer", err_xclk);

	httpd_resp_set_type(req, CONTENT_TYPE_TEXT_PLAIN);

	req_content = cJSON_Parse(buf);
	sensor_t *sensor = esp_camera_sensor_get();

	int xclk = JSON_GET_INT(req_content, "xclk");

	if (xclk == JSON_INT_ATTR_NOTFOUND) {
		httpd_resp_set_status(req, _400_BAD_REQUEST);
		res = httpd_resp_sendstr(req, "Invalid content");
		APP_ERROR_CHECK(false, "", err_xclk);
	}

	if(xclk < MIN_XCLK_MHZ || xclk > MAX_XCLK_MHZ) {
		char message[33];
		sprintf(message, "Value should be between %d and %d", MIN_XCLK_MHZ, MAX_XCLK_MHZ);
		httpd_resp_set_status(req, _400_BAD_REQUEST);
		res = httpd_resp_sendstr(req, message);
		APP_ERROR_CHECK(false, "", err_xclk);
	}

	if(!!sensor->set_xclk(sensor, LEDC_TIMER_0, xclk)) {
		httpd_resp_set_status(req, _500_INTERNAL_SERVER_ERROR);
		res = httpd_resp_sendstr(req, "Something wrong");
		APP_ERROR_CHECK(false, "", err_xclk);
	}

	cJSON_Delete(req_content);

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	res = httpd_resp_send(req, NULL, 0);

	ESP_LOGI(APP_HTTPD_TAG, "Set XCLK: %d MHz", xclk);
	return res;
err_xclk:
	if(!!req_content) cJSON_Delete(req_content);
	return res;
}

static esp_err_t cam_reg_handler(httpd_req_t *req) {
	esp_err_t res;
	cJSON *req_content = NULL;
	char *buf;

	APP_ERROR_CHECK(!!(buf = getBuffer(req, &res)), "An error occurred while loading the buffer", err_reg);

	httpd_resp_set_type(req, CONTENT_TYPE_TEXT_PLAIN);

	req_content = cJSON_Parse(buf);
	sensor_t *sensor = esp_camera_sensor_get();

	int reg = JSON_GET_INT(req_content, "reg");
	int mask = JSON_GET_INT(req_content, "mask");
	int val = JSON_GET_INT(req_content, "val");

	if (reg == JSON_INT_ATTR_NOTFOUND || mask == JSON_INT_ATTR_NOTFOUND || val == JSON_INT_ATTR_NOTFOUND) {
		httpd_resp_set_status(req, _400_BAD_REQUEST);
		res = httpd_resp_sendstr(req, "Invalid content");
		APP_ERROR_CHECK(false, "", err_reg);
	}

	if(!!sensor->set_reg(sensor, reg, mask, val)) {
		httpd_resp_set_status(req, _500_INTERNAL_SERVER_ERROR);
		res = httpd_resp_sendstr(req, "Something wrong");
		APP_ERROR_CHECK(false, "", err_reg);
	}

	cJSON_Delete(req_content);

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	res = httpd_resp_send(req, NULL, 0);

	ESP_LOGI(APP_HTTPD_TAG, "Set Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);
	return res;
err_reg:
	if(!!req_content) cJSON_Delete(req_content);
	return res;
}

static esp_err_t cam_greg_handler(httpd_req_t *req) {
	esp_err_t res;
	cJSON *req_content = NULL;
	char *buf;

	APP_ERROR_CHECK(!!(buf = getBuffer(req, &res)), "An error occurred while loading the buffer", err_greg);

	httpd_resp_set_type(req, CONTENT_TYPE_TEXT_PLAIN);

	req_content = cJSON_Parse(buf);
	sensor_t *sensor = esp_camera_sensor_get();

	int reg = JSON_GET_INT(req_content, "reg");
	int mask = JSON_GET_INT(req_content, "mask");
	int val;

	if (reg == JSON_INT_ATTR_NOTFOUND || mask == JSON_INT_ATTR_NOTFOUND) {
		httpd_resp_set_status(req, _400_BAD_REQUEST);
		res = httpd_resp_sendstr(req, "Invalid content");
		APP_ERROR_CHECK(false, "", err_greg);
	}

	if((val = sensor->get_reg(sensor, reg, mask)) < 0) {
		httpd_resp_set_status(req, _500_INTERNAL_SERVER_ERROR);
		res = httpd_resp_sendstr(req, "Something wrong");
		APP_ERROR_CHECK(false, "", err_greg);
	}

	cJSON_Delete(req_content);

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	char buf_val[20];
	const char * stringval = itoa(val, buf_val, 10);
	res = httpd_resp_send(req, stringval, strlen(stringval));

	ESP_LOGI(APP_HTTPD_TAG, "Get Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);
	return res;
err_greg:
	if(!!req_content) cJSON_Delete(req_content);
	return res;
}
