#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "app_connect.h"

esp_err_t event_handler(void *ctx, system_event_t *event) {
    return ESP_OK;
}

void app_main(void) {
	nvs_flash_init();
	tcpip_adapter_init();
	ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

    ESP_ERROR_CHECK( app_connect() );

    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    int level = 0;
    while (true) {
        gpio_set_level(GPIO_NUM_2, level);
        level = !level;
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}


