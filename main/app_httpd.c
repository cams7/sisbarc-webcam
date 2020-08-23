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

	httpd_register_uri_handler(camera_httpd, &system_info_uri);
	httpd_register_uri_handler(camera_httpd, &cam_status_uri);
	httpd_register_uri_handler(camera_httpd, &cam_capture_uri);
	httpd_register_uri_handler(camera_httpd, &cam_cmd_uri);

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
		APP_ERROR_CHECK(false, "Content too long", err_set_buffer);
	}

	int cur_len = 0;
	buffer = ((rest_server_context_t *)(req->user_ctx))->scratch;

	int received = 0;

	while (cur_len < total_len) {
		received = httpd_req_recv(req, buffer + cur_len, total_len);
		if (received <= 0) {
			/* Respond with 500 Internal Server Error */
			*res = httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to post control value");
			APP_ERROR_CHECK(false, "Failed to post control value", err_set_buffer);
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
	char *buf;

	APP_ERROR_CHECK(!!(buf = getBuffer(req, &res)), "An error occurred while loading the buffer", err_cmd);

	cJSON *root = cJSON_Parse(buf);
	sensor_t *sensor = esp_camera_sensor_get();

	int framesize = JSON_GET_INT(root, "framesize");
	if (framesize != -1)
		if (sensor->pixformat == PIXFORMAT_JPEG)
			APP_ERROR_CHECK(!sensor->set_framesize(sensor, (framesize_t)framesize), "Something wrong", err_cmd_with_resp);

	int quality = JSON_GET_INT(root, "quality");
	if (quality != -1)
		APP_ERROR_CHECK(!sensor->set_quality(sensor, quality), "Something wrong", err_cmd_with_resp);

	int contrast = JSON_GET_INT(root, "contrast");
	if (contrast != -1)
		APP_ERROR_CHECK(!sensor->set_contrast(sensor, contrast), "Something wrong", err_cmd_with_resp);

	int brightness = JSON_GET_INT(root, "brightness");
	if (brightness != -1)
		APP_ERROR_CHECK(!sensor->set_brightness(sensor, brightness), "Something wrong", err_cmd_with_resp);

	int saturation = JSON_GET_INT(root, "saturation");
	if (saturation != -1)
		APP_ERROR_CHECK(!sensor->set_saturation(sensor, saturation), "Something wrong", err_cmd_with_resp);

	int gainceiling = JSON_GET_INT(root, "gainceiling");
	if (gainceiling != -1)
		APP_ERROR_CHECK(!sensor->set_gainceiling(sensor, (gainceiling_t)gainceiling), "Something wrong", err_cmd_with_resp);

	int colorbar = JSON_GET_INT(root, "colorbar");
	if (colorbar != -1)
		APP_ERROR_CHECK(!sensor->set_colorbar(sensor, colorbar), "Something wrong", err_cmd_with_resp);

	int awb = JSON_GET_INT(root, "awb");
	if (awb != -1)
		APP_ERROR_CHECK(!sensor->set_whitebal(sensor, awb), "Something wrong", err_cmd_with_resp);

	int agc = JSON_GET_INT(root, "agc");
	if (agc != -1)
		APP_ERROR_CHECK(!sensor->set_gain_ctrl(sensor, agc), "Something wrong", err_cmd_with_resp);

	int aec = JSON_GET_INT(root, "aec");
	if (aec != -1)
		APP_ERROR_CHECK(!sensor->set_exposure_ctrl(sensor, aec), "Something wrong", err_cmd_with_resp);

	int hmirror = JSON_GET_INT(root, "hmirror");
	if (hmirror != -1)
		APP_ERROR_CHECK(!sensor->set_hmirror(sensor, hmirror), "Something wrong", err_cmd_with_resp);

	int vflip = JSON_GET_INT(root, "vflip");
	if (vflip != -1)
		APP_ERROR_CHECK(!sensor->set_vflip(sensor, vflip), "Something wrong", err_cmd_with_resp);

	int awb_gain = JSON_GET_INT(root, "awb_gain");
	if (awb_gain != -1)
		APP_ERROR_CHECK(!sensor->set_awb_gain(sensor, awb_gain), "Something wrong", err_cmd_with_resp);

	int agc_gain = JSON_GET_INT(root, "agc_gain");
	if (agc_gain != -1)
		APP_ERROR_CHECK(!sensor->set_agc_gain(sensor, agc_gain), "Something wrong", err_cmd_with_resp);

	int aec_value = JSON_GET_INT(root, "aec_value");
	if (aec_value != -1)
		APP_ERROR_CHECK(!sensor->set_aec_value(sensor, aec_value), "Something wrong", err_cmd_with_resp);

	int aec2 = JSON_GET_INT(root, "aec2");
	if (aec2 != -1)
		APP_ERROR_CHECK(!sensor->set_aec2(sensor, aec2), "Something wrong", err_cmd_with_resp);

	int dcw = JSON_GET_INT(root, "dcw");
	if (dcw != -1)
		APP_ERROR_CHECK(!sensor->set_dcw(sensor, dcw), "Something wrong", err_cmd_with_resp);

	int bpc = JSON_GET_INT(root, "bpc");
	if (bpc != -1)
		APP_ERROR_CHECK(!sensor->set_bpc(sensor, bpc), "Something wrong", err_cmd_with_resp);

	int wpc = JSON_GET_INT(root, "wpc");
	if (wpc != -1)
		APP_ERROR_CHECK(!sensor->set_wpc(sensor, wpc), "Something wrong", err_cmd_with_resp);

	int raw_gma = JSON_GET_INT(root, "raw_gma");
	if (raw_gma != -1)
		APP_ERROR_CHECK(!sensor->set_raw_gma(sensor, raw_gma), "Something wrong", err_cmd_with_resp);

	int lenc = JSON_GET_INT(root, "lenc");
	if (lenc != -1)
		APP_ERROR_CHECK(!sensor->set_lenc(sensor, lenc), "Something wrong", err_cmd_with_resp);

	int special_effect = JSON_GET_INT(root, "special_effect");
	if (special_effect != -1)
		APP_ERROR_CHECK(!sensor->set_special_effect(sensor, special_effect), "Something wrong", err_cmd_with_resp);

	int wb_mode = JSON_GET_INT(root, "wb_mode");
	if (wb_mode != -1)
		APP_ERROR_CHECK(!sensor->set_wb_mode(sensor, wb_mode), "Something wrong", err_cmd_with_resp);

	int ae_level = JSON_GET_INT(root, "ae_level");
	if (ae_level != -1)
		APP_ERROR_CHECK(!sensor->set_ae_level(sensor, ae_level), "Something wrong", err_cmd_with_resp);

	ESP_LOGI(APP_HTTPD_TAG,
		"CAM control:\n\tframesize = %d\n\tquality = %d\n\tcontrast = %d\n\tbrightness = %d\n\tsaturation = %d\n\tgainceiling = %d\n\tcolorbar = %d\n\tawb = %d\n\tagc = %d\n\taec = %d\n\thmirror = %d\n\tvflip = %d\n\tawb_gain = %d\n\tagc_gain = %d\n\taec_value = %d\n\taec2 = %d\n\tdcw = %d\n\tbpc = %d\n\twpc = %d\n\traw_gma = %d\n\tlenc = %d\n\tspecial_effect = %d\n\twb_mode = %d\n\tae_level = %d",
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

	cJSON_Delete(root);

	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	res = httpd_resp_send(req, NULL, 0);
	return res;
err_cmd_with_resp:
	cJSON_Delete(root);
	res = httpd_resp_send_500(req);
err_cmd:
	return res;
}
