#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "app_connect.h"
#include "app_camera.h"
#include "app_httpd.h"
#include "app_mdns.h"

#define SISBARC_WEBCAM_TAG "sisbarc-webcam"

#if CONFIG_CAM_WEB_DEPLOY_SF
esp_err_t init_fs(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_CAM_WEB_MOUNT_POINT,
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(SISBARC_WEBCAM_TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(SISBARC_WEBCAM_TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(SISBARC_WEBCAM_TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(SISBARC_WEBCAM_TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(SISBARC_WEBCAM_TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}
#endif

void app_main(void) {
	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	ESP_ERROR_CHECK(init_camera());
    ESP_ERROR_CHECK(app_connect());
#if CONFIG_CAM_WEB_DEPLOY_SF
    ESP_ERROR_CHECK(init_fs());
#endif
    ESP_ERROR_CHECK(init_server(CONFIG_CAM_WEB_MOUNT_POINT));
    ESP_ERROR_CHECK(app_mdns_main());

    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    int level = 0;
    while (true) {
        gpio_set_level(GPIO_NUM_2, level);
        level = !level;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}


