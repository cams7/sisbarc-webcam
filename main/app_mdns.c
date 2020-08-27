/*
 * app_mdns.c
 *
 *  Created on: 27 de ago de 2020
 *      Author: ceanm
 */
#include <string.h>
#include "esp_log.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_camera.h"
#include "sdkconfig.h"

#include "app_common.h"
#include "app_camera.h"
#include "app_mdns.h"

static const char * service_name = "_esp-cam";
static const char * proto = "_tcp";

static SemaphoreHandle_t query_lock = NULL;

static const char * model = NULL;
static char iname[64];
static char hname[64];
static char framesize[4];
static char pixformat[4];

static mdns_result_t * found_cams = NULL;

char* app_mdns_query(size_t * out_len) {
	return "";
}

esp_err_t app_mdns_update_framesize(const int size) {
	snprintf(framesize, 4, "%d", size);
	APP_ERROR_CHECK_WITH_MSG(!mdns_service_txt_item_set(service_name, proto, "framesize", (char*)framesize), "mdns_service_txt_item_set() framesize Failed", err_mdns_update);
	return ESP_OK;
err_mdns_update:
	return ESP_FAIL;
}


static void mdns_task(void *pvParameters);

esp_err_t app_mdns_main(void) {
	uint8_t mac[6];

	query_lock = xSemaphoreCreateBinary();

	APP_ERROR_CHECK_WITH_MSG(query_lock != NULL, "xSemaphoreCreateMutex() Failed", err_app_mdns);

	xSemaphoreGive(query_lock);

	sensor_t * s = esp_camera_sensor_get();

	APP_ERROR_CHECK_WITH_MSG(s != NULL, "Something wrong", err_app_mdns);

	switch(s->id.PID){
		case OV2640_PID:
			model = "OV2640";
			break;
		case OV3660_PID:
			model = "OV3660";
			break;
		case OV5640_PID:
			model = "OV5640";
			break;
		case OV7725_PID:
			model = "OV7725";
			break;
		default:
			model = "UNKNOWN";
			break;
	}

	if (strlen(CONFIG_CAM_HOST_NAME) > 0) {
		snprintf(iname, 64, "%s", CONFIG_CAM_HOST_NAME);
	} else {
		APP_ERROR_CHECK_WITH_MSG(esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK, "esp_read_mac() Failed", err_app_mdns);
		snprintf(iname, 64, "%s-%s-%02X%02X%02X", CAM_BOARD, model, mac[3], mac[4], mac[5]);
	}

	snprintf(framesize, 4, "%d", s->status.framesize);
	snprintf(pixformat, 4, "%d", s->pixformat);

	char * src = iname, *dst = hname, c;
	while (*src) {
		c = *src++;
		if (c >= 'A' && c <= 'Z') {
			c -= 'A' - 'a';
		}
		*dst++ = c;
	}
	*dst++ = '\0';

	APP_ERROR_CHECK_WITH_MSG(mdns_init() == ESP_OK, "mdns_init() Failed", err_app_mdns);
	APP_ERROR_CHECK_WITH_MSG(mdns_hostname_set(hname) == ESP_OK, "mdns_hostname_set(hname) Failed", err_app_mdns);
	APP_ERROR_CHECK_WITH_MSG(mdns_instance_name_set(iname) == ESP_OK, "mdns_instance_name_set(iname) Failed", err_app_mdns);
	APP_ERROR_CHECK_WITH_MSG(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0) == ESP_OK, "mdns_service_add() HTTP Failed", err_app_mdns);

	mdns_txt_item_t camera_txt_data[] = {
		{(char*)"board"         ,(char*)CAM_BOARD},
		{(char*)"model"     	,(char*)model},
		{(char*)"stream_port"   ,(char*)"81"},
		{(char*)"framesize"   	,(char*)framesize},
		{(char*)"pixformat"   	,(char*)pixformat}
	};

	APP_ERROR_CHECK_WITH_MSG(!mdns_service_add(NULL, service_name, proto, 80, camera_txt_data, 5), "mdns_service_add() ESP-CAM Failed", err_app_mdns);

	xTaskCreatePinnedToCore(mdns_task, "mdns-cam", configMINIMAL_STACK_SIZE * 3, NULL, 2, NULL, APP_CPU_NUM);

	ESP_LOGI(APP_MDNS_TAG, "mdns_hostname: %s, mdns_instance_name: %s", hname, iname);

	return ESP_OK;
err_app_mdns:
	return ESP_FAIL;
}

static esp_err_t mdns_query_for_cams(void) {
	mdns_result_t * new_cams = NULL;
	esp_err_t resp;
	APP_ERROR_CHECK((resp = mdns_query_ptr(service_name, proto, 5000, 4,  &new_cams)) == ESP_OK, err_mdns_query);

	xSemaphoreTake(query_lock, portMAX_DELAY);
	if (!found_cams)
		mdns_query_results_free(found_cams);

	found_cams = new_cams;
	xSemaphoreGive(query_lock);

	return ESP_OK;
err_mdns_query:
	return resp;
}

static void mdns_task(void *pvParameters) {
	esp_err_t resp;
	for (;;) {
		if((resp = mdns_query_for_cams()) != ESP_OK)
			ESP_LOGE(APP_MDNS_TAG, "MDNS Query Failed: %s", esp_err_to_name(resp));
		//delay 55 seconds
		vTaskDelay((55 * 1000) / portTICK_PERIOD_MS);
	}
	vTaskDelete(NULL);
}
