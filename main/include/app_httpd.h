/*
 * app_httpd.h
 *
 *  Created on: 22 de ago de 2020
 *      Author: ceanm
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

#define APP_HTTPD_TAG "app_httpd"

esp_err_t init_server(const char *base_path);

#ifdef __cplusplus
}
#endif
