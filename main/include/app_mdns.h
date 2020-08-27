/*
 * app_mdns.h
 *
 *  Created on: 27 de ago de 2020
 *      Author: ceanm
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include "esp_err.h"

#define APP_MDNS_TAG "app_mdns"

char* app_mdns_query(size_t* out_len);

esp_err_t app_mdns_update_framesize(const int size);

esp_err_t app_mdns_main(void);

#ifdef __cplusplus
}
#endif
