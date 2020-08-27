/*
 * app_httpd.c
 *
 *  Created on: 22 de ago de 2020
 *      Author: ceanm
 */

#include <stdarg.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_vfs.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "cJSON.h"
#include "sdkconfig.h"

#include "app_common.h"
#include "app_httpd_common.h"
#include "app_camera.h"
#include "app_httpd.h"

#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"

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
static esp_err_t cam_pll_handler(httpd_req_t *req);
static esp_err_t cam_win_handler(httpd_req_t *req);

typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

esp_err_t init_server(void) {
	rest_server_context_t *rest_context = NULL;
	rest_context = calloc(1, sizeof(rest_server_context_t));
	APP_ERROR_CHECK_WITH_MSG(!!rest_context, "No memory for rest context", err_init);

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.max_uri_handlers = 10;

	config.uri_match_fn = httpd_uri_match_wildcard;

	ESP_LOGI(APP_HTTPD_TAG, "Starting HTTP Server");
	APP_ERROR_CHECK_WITH_MSG(httpd_start(&camera_httpd, &config) == ESP_OK, "Start web server failed", err_init);

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

	httpd_uri_t cam_pll_uri = {
		.uri = "/api/v1/cam/pll",
		.method = HTTP_POST,
		.handler = cam_pll_handler,
		.user_ctx = rest_context
	};

	httpd_uri_t cam_win_uri = {
		.uri = "/api/v1/cam/resolution",
		.method = HTTP_POST,
		.handler = cam_win_handler,
		.user_ctx = rest_context
	};

	httpd_register_uri_handler(camera_httpd, &system_info_uri);
	httpd_register_uri_handler(camera_httpd, &cam_status_uri);
	httpd_register_uri_handler(camera_httpd, &cam_capture_uri);
	httpd_register_uri_handler(camera_httpd, &cam_cmd_uri);
	httpd_register_uri_handler(camera_httpd, &cam_xclk_uri);
	httpd_register_uri_handler(camera_httpd, &cam_reg_uri);
	httpd_register_uri_handler(camera_httpd, &cam_greg_uri);
	httpd_register_uri_handler(camera_httpd, &cam_pll_uri);
	httpd_register_uri_handler(camera_httpd, &cam_win_uri);

	config.server_port += 1;
	config.ctrl_port += 1;
	APP_ERROR_CHECK_WITH_MSG(httpd_start(&stream_httpd, &config) == ESP_OK, "Start stream server failed", err_init);

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

static void get_chip_info(esp_chip_info_t *chip_info, cJSON *resp_json_data) {
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

    cJSON_AddItemToObject(resp_json_data, "chip", chip);
}

static void get_flash_info(esp_chip_info_t *chip_info, cJSON *resp_json_data) {
    cJSON *flash = cJSON_CreateObject();

    char flash_size[7];
    sprintf(flash_size, "%dMB", spi_flash_get_chip_size() / (1024 * 1024));
    cJSON_AddStringToObject(flash, "size", flash_size);

    cJSON_AddStringToObject(flash, "type", (chip_info->features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    cJSON_AddItemToObject(resp_json_data, "flash", flash);
}

static esp_err_t system_info_handler(httpd_req_t *req) {
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);

	cJSON *resp_json_data = cJSON_CreateObject();
	get_chip_info(&chip_info, resp_json_data);
	get_flash_info(&chip_info, resp_json_data);

	esp_err_t resp = resp_send_json_data_ok(req, resp_json_data);

	cJSON_Delete(resp_json_data);
	return resp;
}

static esp_err_t cam_status_handler(httpd_req_t *req) {
	sensor_t *sensor = esp_camera_sensor_get();

	cJSON *resp_json_data = cJSON_CreateObject();

	cJSON_AddStringToObject(resp_json_data, "board", CAM_BOARD);
	cJSON_AddNumberToObject(resp_json_data, "xclk", sensor->xclk_freq_hz / 1000000);
	cJSON_AddNumberToObject(resp_json_data, "pixformat", sensor->pixformat);
	cJSON_AddNumberToObject(resp_json_data, "framesize", sensor->status.framesize);
	cJSON_AddNumberToObject(resp_json_data, "quality", sensor->status.quality);
	cJSON_AddNumberToObject(resp_json_data, "brightness", sensor->status.brightness);
	cJSON_AddNumberToObject(resp_json_data, "contrast", sensor->status.contrast);
	cJSON_AddNumberToObject(resp_json_data, "saturation", sensor->status.saturation);
	cJSON_AddNumberToObject(resp_json_data, "sharpness", sensor->status.sharpness);
	cJSON_AddNumberToObject(resp_json_data, "special_effect", sensor->status.special_effect);
	cJSON_AddNumberToObject(resp_json_data, "wb_mode", sensor->status.wb_mode);
	cJSON_AddNumberToObject(resp_json_data, "awb", sensor->status.awb);
	cJSON_AddNumberToObject(resp_json_data, "awb_gain", sensor->status.awb_gain);
	cJSON_AddNumberToObject(resp_json_data, "aec", sensor->status.aec);
	cJSON_AddNumberToObject(resp_json_data, "aec2", sensor->status.aec2);
	cJSON_AddNumberToObject(resp_json_data, "ae_level", sensor->status.ae_level);
	cJSON_AddNumberToObject(resp_json_data, "aec_value", sensor->status.aec_value);
	cJSON_AddNumberToObject(resp_json_data, "agc", sensor->status.agc);
	cJSON_AddNumberToObject(resp_json_data, "agc_gain", sensor->status.agc_gain);
	cJSON_AddNumberToObject(resp_json_data, "gainceiling", sensor->status.gainceiling);
	cJSON_AddNumberToObject(resp_json_data, "bpc", sensor->status.bpc);
	cJSON_AddNumberToObject(resp_json_data, "wpc", sensor->status.wpc);
	cJSON_AddNumberToObject(resp_json_data, "raw_gma", sensor->status.raw_gma);
	cJSON_AddNumberToObject(resp_json_data, "lenc", sensor->status.lenc);
	cJSON_AddNumberToObject(resp_json_data, "hmirror", sensor->status.hmirror);
	cJSON_AddNumberToObject(resp_json_data, "dcw", sensor->status.dcw);
	cJSON_AddNumberToObject(resp_json_data, "colorbar", sensor->status.colorbar);
	cJSON_AddNumberToObject(resp_json_data, "led_intensity", -1);

	esp_err_t resp = resp_send_json_data_ok(req, resp_json_data);

	cJSON_Delete(resp_json_data);
	return resp;
}

static esp_err_t cam_stream_handler(httpd_req_t *req) {
	esp_err_t resp;
	camera_fb_t *fb = NULL;
	struct timeval _timestamp;

	size_t _jpg_buf_len = 0;
	uint8_t *_jpg_buf = NULL;
	char *part_buf[128];

//	if (!last_frame)
//		last_frame = esp_timer_get_time();

	APP_ERROR_CHECK_WITH_MSG(httpd_resp_set_type(req, _STREAM_CONTENT_TYPE) == ESP_OK, "Stream content type invalid", err_stream_with_resp);

	httpd_resp_set_hdr(req, HTTP_HEAD_ALLOW_ORIGIN, "*");
	httpd_resp_set_hdr(req, "X-Framerate", "60");

	while (true) {
		fb = esp_camera_fb_get();
		APP_ERROR_CHECK_WITH_MSG(!!fb, "Camera capture failed", err_stream_with_resp);

		_timestamp.tv_sec = fb->timestamp.tv_sec;
		_timestamp.tv_usec = fb->timestamp.tv_usec;
		if (fb->format != PIXFORMAT_JPEG) {
			bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
			APP_ERROR_CHECK_WITH_MSG(jpeg_converted, "JPEG compression failed", err_stream_with_resp);
		} else {
			_jpg_buf_len = fb->len;
			_jpg_buf = fb->buf;
		}

		APP_ERROR_CHECK_WITH_MSG((resp = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY))) == ESP_OK, "Error sending chunk", err_stream);

		size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
		APP_ERROR_CHECK_WITH_MSG((resp = httpd_resp_send_chunk(req, (const char *)part_buf, hlen)) == ESP_OK, "Error sending chunk (part buffer)", err_stream);

		APP_ERROR_CHECK_WITH_MSG((resp = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len)) == ESP_OK, "Error sending chunk (jpg buffer)", err_stream);

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

	resp = ESP_OK;
	return resp;
err_stream_with_resp:
	resp = resp_send_json_message(req, _500_INTERNAL_SERVER_ERROR, ERR_MSG_SOMETHING_WRONG);
err_stream:
	if (!!fb) esp_camera_fb_return(fb);
	return resp;
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
	esp_err_t resp;
	int64_t fr_start = esp_timer_get_time();

	camera_fb_t *fb = esp_camera_fb_get();

	APP_ERROR_CHECK_WITH_MSG(!!fb, "Camera capture failed", err_capture_with_resp);

	httpd_resp_set_type(req, CONTENT_TYPE_IMAGE_JPEG);
	httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
	httpd_resp_set_hdr(req, HTTP_HEAD_ALLOW_ORIGIN, "*");

	char ts[32];
	snprintf(ts, 32, "%ld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
	httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

	size_t fb_len = 0;
	if (fb->format == PIXFORMAT_JPEG) {
		fb_len = fb->len;
		APP_ERROR_CHECK_WITH_MSG((resp = httpd_resp_send(req, (const char *)fb->buf, fb->len)) == ESP_OK, ERR_MSG_SOMETHING_WRONG, err_capture);
	} else {
		jpg_chunking_t jchunk = {req, 0};
		APP_ERROR_CHECK_WITH_MSG(frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk), "Error when converting camera frame buffer to JPEG", err_capture_with_resp);
		resp = httpd_resp_send_chunk(req, NULL, 0);
		fb_len = jchunk.len;
	}
	esp_camera_fb_return(fb);
	fb = NULL;

	int64_t fr_end = esp_timer_get_time();
	ESP_LOGI(APP_HTTPD_TAG, "JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));

	return resp;
err_capture_with_resp:
	resp = resp_send_json_message(req, _500_INTERNAL_SERVER_ERROR, ERR_MSG_SOMETHING_WRONG);
err_capture:
	if (!!fb) esp_camera_fb_return(fb);
	return resp;
}

static void setSensorIntVal(sensor_t *sensor, cJSON *resp_json_err, cJSON *resp_json_data, const char* attr, int* val, bool *hasError, int (*f)(sensor_t*, int)) {
	if(*val != JSON_INT_ATTR_NOTFOUND) {
		if (!(*f)(sensor, *val))
			cJSON_AddNumberToObject(resp_json_data, attr, *val);
		else {
			cJSON_AddStringToObject(resp_json_err, attr, ERR_MSG_SOMETHING_WRONG);
			*hasError = true;
		}
	}
}

static esp_err_t cam_cmd_handler(httpd_req_t *req) {
	esp_err_t resp;
	cJSON *resp_json_err = NULL;
	cJSON *resp_json_data = NULL;
	char *buf;

	APP_ERROR_CHECK_WITH_MSG(!!(buf = getBuffer(req, &resp)), ERR_MSG_REQ_JSON_DATA_LOADING_BUFFER, err_cmd);

	cJSON *req_json_data = cJSON_Parse(buf);
	sensor_t *sensor = esp_camera_sensor_get();
	
	bool hasError = false;
	resp_json_err = cJSON_CreateObject();

	int framesize = getAttrIntVal(req_json_data, resp_json_err, "framesize", VAL_BETWEEN, sensor->pixformat == PIXFORMAT_JPEG, &hasError, 2, MIN_FRAMESIZE, MAX_FRAMESIZE);
	int quality = getAttrIntVal(req_json_data, resp_json_err, "quality", VAL_BETWEEN, true, &hasError, 2, MIN_QUALITY, MAX_QUALITY);
	int contrast = getAttrIntVal(req_json_data, resp_json_err, "contrast", VAL_BETWEEN, true, &hasError, 2, MIN_CONTRAST, MAX_CONTRAST);
	int brightness = getAttrIntVal(req_json_data, resp_json_err, "brightness", VAL_BETWEEN, true, &hasError, 2, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
	int saturation = getAttrIntVal(req_json_data, resp_json_err, "saturation", VAL_BETWEEN, true, &hasError, 2, MIN_SATURATION, MAX_SATURATION);
	int gainceiling = getAttrIntVal(req_json_data, resp_json_err, "gainceiling", VAL_BETWEEN, true, &hasError, 2, MIN_GAINCEILING, MAX_GAINCEILING);
	int colorbar = getAttrIntVal(req_json_data, resp_json_err, "colorbar", VAL_BOOL, true, &hasError, 0);
	int awb = getAttrIntVal(req_json_data, resp_json_err, "awb", VAL_BOOL, true, &hasError, 0);
	int agc = getAttrIntVal(req_json_data, resp_json_err, "agc", VAL_BOOL, true, &hasError, 0);
	int aec = getAttrIntVal(req_json_data, resp_json_err, "aec", VAL_BOOL, true, &hasError, 0);
	int hmirror = getAttrIntVal(req_json_data, resp_json_err, "hmirror", VAL_BOOL, true, &hasError, 0);
	int vflip = getAttrIntVal(req_json_data, resp_json_err, "vflip", VAL_BOOL, true, &hasError, 0);
	int awb_gain = getAttrIntVal(req_json_data, resp_json_err, "awb_gain", VAL_BOOL, true, &hasError, 0);
	int agc_gain = getAttrIntVal(req_json_data, resp_json_err, "agc_gain", VAL_BETWEEN, true, &hasError, 2, MIN_AGC_GAIN, MAX_AGC_GAIN);
	int aec_value = getAttrIntVal(req_json_data, resp_json_err, "aec_value", VAL_BETWEEN, true, &hasError, 2, MIN_AEC_VALUE, MAX_AEC_VALUE);
	int aec2 = getAttrIntVal(req_json_data, resp_json_err, "aec2", VAL_BOOL, true, &hasError, 0);
	int dcw = getAttrIntVal(req_json_data, resp_json_err, "dcw", VAL_BOOL, true, &hasError, 0);
	int bpc = getAttrIntVal(req_json_data, resp_json_err, "bpc", VAL_BOOL, true, &hasError, 0);
	int wpc = getAttrIntVal(req_json_data, resp_json_err, "wpc", VAL_BOOL, true, &hasError, 0);
	int raw_gma = getAttrIntVal(req_json_data, resp_json_err, "raw_gma", VAL_BOOL, true, &hasError, 0);
	int lenc = getAttrIntVal(req_json_data, resp_json_err, "lenc", VAL_BOOL, true, &hasError, 0);
	int special_effect = getAttrIntVal(req_json_data, resp_json_err, "special_effect", VAL_BETWEEN, true, &hasError, 2, MIN_SPECIAL_EFFECT, MAX_SPECIAL_EFFECT);
	int wb_mode = getAttrIntVal(req_json_data, resp_json_err, "wb_mode", VAL_BETWEEN, true, &hasError, 2, MIN_WB_MODE, MAX_WB_MODE);
	int ae_level = getAttrIntVal(req_json_data, resp_json_err, "ae_level", VAL_BETWEEN, true, &hasError, 2, MIN_AE_LEVEL, MAX_AE_LEVEL);

	cJSON_Delete(req_json_data);
	req_json_data = NULL;

	if(hasError) {
		resp = resp_send_json_data(req, resp_json_err, _400_BAD_REQUEST);
		APP_ERROR(err_cmd);
	}

	resp_json_data = cJSON_CreateObject();

	//Resolution
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "framesize", &framesize, &hasError, sensor->set_framesize);
	//Quality
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "quality", &quality, &hasError, sensor->set_quality);
	//Contrast
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "contrast", &contrast, &hasError, sensor->set_contrast);
	//Brightness
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "brightness", &brightness, &hasError, sensor->set_brightness);
	//Saturation
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "saturation", &saturation, &hasError, sensor->set_saturation);
	//Gain Ceiling
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "gainceiling", &gainceiling, &hasError, sensor->set_gainceiling);
	//Color Bar
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "colorbar", &colorbar, &hasError, sensor->set_colorbar);
	//AWB
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "awb", &awb, &hasError, sensor->set_whitebal);
	//AGC
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "agc", &agc, &hasError, sensor->set_gain_ctrl);
	//AEC SENSOR
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "aec", &aec, &hasError, sensor->set_exposure_ctrl);
	//H-Mirror
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "hmirror", &hmirror, &hasError, sensor->set_hmirror);
	//V-Flip
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "vflip", &vflip, &hasError, sensor->set_vflip);
	//AWB Gain
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "awb_gain", &awb_gain, &hasError, sensor->set_awb_gain);
	//Gain
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "agc_gain", &agc_gain, &hasError, sensor->set_agc_gain);
	//Exposure
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "aec_value", &aec_value, &hasError, sensor->set_aec_value);
	//AEC DSP
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "aec2", &aec2, &hasError, sensor->set_aec2);
	//DCW (Downsize EN)
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "dcw", &dcw, &hasError, sensor->set_dcw);
	//BPC
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "bpc", &bpc, &hasError, sensor->set_bpc);
	//WPC
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "wpc", &wpc, &hasError, sensor->set_wpc);
	//Raw GMA
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "raw_gma", &raw_gma, &hasError, sensor->set_raw_gma);
	//Lens Correction
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "lenc", &lenc, &hasError, sensor->set_lenc);
	//Special Effect
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "special_effect", &special_effect, &hasError, sensor->set_special_effect);
	//WB Mode
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "wb_mode", &wb_mode, &hasError, sensor->set_wb_mode);
	//AE Level
	setSensorIntVal(sensor, resp_json_err, resp_json_data, "ae_level", &ae_level, &hasError, sensor->set_ae_level);

	if(hasError) {
		resp = resp_send_json_data(req, resp_json_err, _500_INTERNAL_SERVER_ERROR);
		APP_ERROR(err_cmd);
	}

	cJSON_Delete(resp_json_err);
	resp_json_err = NULL;

	resp = resp_send_json_data_ok(req, resp_json_data);

	cJSON_Delete(resp_json_data);
	resp_json_data = NULL;

	return resp;
err_cmd:
	if(!!resp_json_err) cJSON_Delete(resp_json_err);
	if(!!resp_json_data) cJSON_Delete(resp_json_data);
	return resp;
}

static esp_err_t cam_xclk_handler(httpd_req_t *req) {
	esp_err_t resp;
	cJSON *resp_json_err = NULL;
	cJSON *resp_json_data = NULL;
	char *buf;

	APP_ERROR_CHECK_WITH_MSG(!!(buf = getBuffer(req, &resp)), ERR_MSG_REQ_JSON_DATA_LOADING_BUFFER, err_xclk);

	cJSON *req_json_data = cJSON_Parse(buf);

	bool hasError = false;
	resp_json_err = cJSON_CreateObject();

	int xclk = getAttrIntVal(req_json_data, resp_json_err, "xclk", VAL_BETWEEN, true, &hasError, 2, MIN_XCLK_MHZ, MAX_XCLK_MHZ);

	cJSON_Delete(req_json_data);
	req_json_data = NULL;

	if(xclk == JSON_INT_ATTR_NOTFOUND) {
		resp = resp_send_json_invalid_content(req);
		APP_ERROR(err_xclk);
	}

	if(hasError) {
		resp = resp_send_json_data(req, resp_json_err, _400_BAD_REQUEST);
		APP_ERROR(err_xclk);
	}

	cJSON_Delete(resp_json_err);
	resp_json_err = NULL;

	resp_json_data = cJSON_CreateObject();
	sensor_t *sensor = esp_camera_sensor_get();

	if (!sensor->set_xclk(sensor, LEDC_TIMER_0, xclk))
		cJSON_AddNumberToObject(resp_json_data, "xclk", xclk);
	else {
		resp = resp_send_json_message(req, _500_INTERNAL_SERVER_ERROR, ERR_MSG_SOMETHING_WRONG);
		APP_ERROR(err_xclk);
	}

	resp = resp_send_json_data_ok(req, resp_json_data);

	cJSON_Delete(resp_json_data);
	resp_json_data = NULL;

	return resp;
err_xclk:
	if(!!resp_json_err) cJSON_Delete(resp_json_err);
	if(!!resp_json_data) cJSON_Delete(resp_json_data);
	return resp;
}

static esp_err_t cam_reg_handler(httpd_req_t *req) {
	esp_err_t resp;
	cJSON *resp_json_data = NULL;
	char *buf;

	APP_ERROR_CHECK_WITH_MSG(!!(buf = getBuffer(req, &resp)), ERR_MSG_REQ_JSON_DATA_LOADING_BUFFER, err_reg);

	cJSON *req_json_data = cJSON_Parse(buf);

	int reg = JSON_GET_INT(req_json_data, "reg");
	int mask = JSON_GET_INT(req_json_data, "mask");
	int val = JSON_GET_INT(req_json_data, "val");

	cJSON_Delete(req_json_data);
	req_json_data = NULL;

	if(reg == JSON_INT_ATTR_NOTFOUND || mask == JSON_INT_ATTR_NOTFOUND || val == JSON_INT_ATTR_NOTFOUND) {
		resp = resp_send_json_invalid_content(req);
		APP_ERROR(err_reg);
	}

	resp_json_data = cJSON_CreateObject();
	sensor_t *sensor = esp_camera_sensor_get();

	if (!sensor->set_reg(sensor, reg, mask, val)) {
		cJSON_AddNumberToObject(resp_json_data, "reg", reg);
		cJSON_AddNumberToObject(resp_json_data, "mask", mask);
		cJSON_AddNumberToObject(resp_json_data, "val", val);
	} else {
		resp = resp_send_json_message(req, _500_INTERNAL_SERVER_ERROR, ERR_MSG_SOMETHING_WRONG);
		APP_ERROR(err_reg);
	}

	resp = resp_send_json_data_ok(req, resp_json_data);

	cJSON_Delete(resp_json_data);
	resp_json_data = NULL;

	return resp;
err_reg:
	if(!!resp_json_data) cJSON_Delete(resp_json_data);
	return resp;
}

static esp_err_t cam_greg_handler(httpd_req_t *req) {
	esp_err_t resp;
	cJSON *resp_json_data = NULL;
	char *buf;

	APP_ERROR_CHECK_WITH_MSG(!!(buf = getBuffer(req, &resp)), ERR_MSG_REQ_JSON_DATA_LOADING_BUFFER, err_greg);

	cJSON *req_json_data = cJSON_Parse(buf);

	int reg = JSON_GET_INT(req_json_data, "reg");
	int mask = JSON_GET_INT(req_json_data, "mask");
	int val;

	cJSON_Delete(req_json_data);
	req_json_data = NULL;

	if(reg == JSON_INT_ATTR_NOTFOUND || mask == JSON_INT_ATTR_NOTFOUND) {
		resp = resp_send_json_invalid_content(req);
		APP_ERROR(err_greg);
	}

	resp_json_data = cJSON_CreateObject();
	sensor_t *sensor = esp_camera_sensor_get();

	if ((val = sensor->get_reg(sensor, reg, mask)) >= 0) {
		cJSON_AddNumberToObject(resp_json_data, "reg", reg);
		cJSON_AddNumberToObject(resp_json_data, "mask", mask);
		cJSON_AddNumberToObject(resp_json_data, "val", val);
	} else {
		resp = resp_send_json_message(req, _500_INTERNAL_SERVER_ERROR, ERR_MSG_SOMETHING_WRONG);
		APP_ERROR(err_greg);
	}

	resp = resp_send_json_data_ok(req, resp_json_data);

	cJSON_Delete(resp_json_data);
	resp_json_data = NULL;

	return resp;
err_greg:
	if(!!resp_json_data) cJSON_Delete(resp_json_data);
	return resp;
}

static int getAttrIntValOrZero(cJSON *req_json_data, const char* attr) {
	int val = JSON_GET_INT(req_json_data, attr);
	if(val == JSON_INT_ATTR_NOTFOUND)
		val = 0;
	return val;
}

static esp_err_t cam_pll_handler(httpd_req_t *req) {
	esp_err_t resp;
	cJSON *resp_json_data = NULL;
	char *buf;

	APP_ERROR_CHECK_WITH_MSG(!!(buf = getBuffer(req, &resp)), ERR_MSG_REQ_JSON_DATA_LOADING_BUFFER, err_pll);

	cJSON *req_json_data = cJSON_Parse(buf);

	int bypass = getAttrIntValOrZero(req_json_data, "bypass");
	int mul = getAttrIntValOrZero(req_json_data, "mul");
	int sys = getAttrIntValOrZero(req_json_data, "sys");
	int root = getAttrIntValOrZero(req_json_data, "root");
	int pre = getAttrIntValOrZero(req_json_data, "pre");
	int seld5 = getAttrIntValOrZero(req_json_data, "seld5");
	int pclken = getAttrIntValOrZero(req_json_data, "pclken");
	int pclk = getAttrIntValOrZero(req_json_data, "pclk");

	cJSON_Delete(req_json_data);
	req_json_data = NULL;

	resp_json_data = cJSON_CreateObject();
	sensor_t *sensor = esp_camera_sensor_get();

	if (!sensor->set_pll(sensor, bypass, mul, sys, root, pre, seld5, pclken, pclk)) {
		cJSON_AddNumberToObject(resp_json_data, "bypass", bypass);
		cJSON_AddNumberToObject(resp_json_data, "mul", mul);
		cJSON_AddNumberToObject(resp_json_data, "sys", sys);
		cJSON_AddNumberToObject(resp_json_data, "root", root);
		cJSON_AddNumberToObject(resp_json_data, "pre", pre);
		cJSON_AddNumberToObject(resp_json_data, "seld5", seld5);
		cJSON_AddNumberToObject(resp_json_data, "pclken", pclken);
		cJSON_AddNumberToObject(resp_json_data, "pclk", pclk);
	} else {
		resp = resp_send_json_message(req, _500_INTERNAL_SERVER_ERROR, ERR_MSG_SOMETHING_WRONG);
		APP_ERROR(err_pll);
	}

	resp = resp_send_json_data_ok(req, resp_json_data);

	cJSON_Delete(resp_json_data);
	resp_json_data = NULL;

	return resp;
err_pll:
	if(!!resp_json_data) cJSON_Delete(resp_json_data);
	return resp;
}

static esp_err_t cam_win_handler(httpd_req_t *req) {
	esp_err_t resp;
	cJSON *resp_json_err = NULL;
	cJSON *resp_json_data = NULL;
	char *buf;

	APP_ERROR_CHECK_WITH_MSG(!!(buf = getBuffer(req, &resp)), ERR_MSG_REQ_JSON_DATA_LOADING_BUFFER, err_win);

	cJSON *req_json_data = cJSON_Parse(buf);

	bool hasError = false;
	resp_json_err = cJSON_CreateObject();

	int startX = getAttrIntVal(req_json_data, resp_json_err, "sx", VAL_BETWEEN, true, &hasError, 2, MIN_RESOLUTION_START_X, MAX_RESOLUTION_START_X);
	int startY = getAttrIntValOrZero(req_json_data, "sy");
	int endX = getAttrIntValOrZero(req_json_data, "ex");
	int endY = getAttrIntValOrZero(req_json_data, "ey");
	int offsetX = getAttrIntValOrZero(req_json_data, "offx");
	int offsetY = getAttrIntValOrZero(req_json_data, "offy");
	int totalX = getAttrIntValOrZero(req_json_data, "tx");
	int totalY = getAttrIntValOrZero(req_json_data, "ty");
	int outputX = getAttrIntValOrZero(req_json_data, "ox");
	int outputY = getAttrIntValOrZero(req_json_data, "oy");
	int scale = getAttrIntVal(req_json_data, resp_json_err, "scale", VAL_BOOL, true, &hasError, 0);
	int binning = getAttrIntVal(req_json_data, resp_json_err, "binning", VAL_BOOL, true, &hasError, 0);

	cJSON_Delete(req_json_data);
	req_json_data = NULL;

	if(hasError) {
		resp = resp_send_json_data(req, resp_json_err, _400_BAD_REQUEST);
		APP_ERROR(err_win);
	}

	cJSON_Delete(resp_json_err);
	resp_json_err = NULL;

	resp_json_data = cJSON_CreateObject();
	sensor_t *s = esp_camera_sensor_get();

	if (!s->set_res_raw(s, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning)) {
		cJSON_AddNumberToObject(resp_json_data, "sx", startX);
		cJSON_AddNumberToObject(resp_json_data, "sy", startY);
		cJSON_AddNumberToObject(resp_json_data, "ex", endX);
		cJSON_AddNumberToObject(resp_json_data, "ey", endY);
		cJSON_AddNumberToObject(resp_json_data, "offx", offsetX);
		cJSON_AddNumberToObject(resp_json_data, "offy", offsetY);
		cJSON_AddNumberToObject(resp_json_data, "tx", totalX);
		cJSON_AddNumberToObject(resp_json_data, "ty", totalY);
		cJSON_AddNumberToObject(resp_json_data, "ox", outputX);
		cJSON_AddNumberToObject(resp_json_data, "oy", outputY);
		cJSON_AddNumberToObject(resp_json_data, "scale", scale);
		cJSON_AddNumberToObject(resp_json_data, "binning", binning);
	} else {
		resp = resp_send_json_message(req, _500_INTERNAL_SERVER_ERROR, ERR_MSG_SOMETHING_WRONG);
		APP_ERROR(err_win);
	}

	resp = resp_send_json_data_ok(req, resp_json_data);

	cJSON_Delete(resp_json_data);
	resp_json_data = NULL;

	return resp;

err_win:
	if(!!resp_json_err) cJSON_Delete(resp_json_err);
	if(!!resp_json_data) cJSON_Delete(resp_json_data);
	return resp;
}
