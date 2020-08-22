/*
 * cam_server.c
 *
 *  Created on: 22 de ago de 2020
 *      Author: ceanm
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "sdkconfig.h"

#include "app_common.h"
#include "cam_server.h"

#ifdef CONFIG_IDF_TARGET_ESP32
#define CHIP_NAME "ESP32"
#endif

static esp_err_t system_info_get_handler(httpd_req_t *req);

esp_err_t start_cam_server(void) {
	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.uri_match_fn = httpd_uri_match_wildcard;

	ESP_LOGI(CAM_SERVER_TAG, "Starting HTTP Server");
	APP_ERROR_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);

	/* URI handler for fetching system info */
	httpd_uri_t system_info_get_uri = {
		.uri = "/api/v1/system/info",
		.method = HTTP_GET,
		.handler = system_info_get_handler,
		.user_ctx = NULL
	};
	httpd_register_uri_handler(server, &system_info_get_uri);

	return ESP_OK;
err_start:
	return ESP_FAIL;
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

static esp_err_t system_info_get_handler(httpd_req_t *req) {
	httpd_resp_set_type(req, "application/json");
	cJSON *root = cJSON_CreateObject();
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);

	get_chip_info(&chip_info, root);
	get_flash_info(&chip_info, root);

	const char *sys_info = cJSON_Print(root);
	httpd_resp_sendstr(req, sys_info);
	free((void *)sys_info);

	cJSON_Delete(root);

	return ESP_OK;
}
