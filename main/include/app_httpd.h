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

#define CONTENT_TYPE_TEXT_PLAIN "text/plain"
#define CONTENT_TYPE_TEXT_HTML  "text/html"
#define CONTENT_TYPE_TEXT_XML   "text/xml"
#define CONTENT_TYPE_TEXT_CSS   "text/css"
#define CONTENT_TYPE_APPLICATION_JSON       "application/json"
#define CONTENT_TYPE_APPLICATION_JAVASCRIPT "application/javascript"
#define CONTENT_TYPE_IMAGE_PNG   "image/png"
#define CONTENT_TYPE_IMAGE_JPEG  "image/jpeg"
#define CONTENT_TYPE_IMAGE_XICON "image/x-icon"

#define _200_OK                    "200"
#define _400_BAD_REQUEST           "400"
#define _500_INTERNAL_SERVER_ERROR "500"

#define ERR_MSG_SOMETHING_WRONG "Something wrong"
#define ERR_MSG_INVALID_CONTENT "Invalid content"

#define APP_HTTPD_TAG "app_httpd"

esp_err_t init_server(void);

#ifdef __cplusplus
}
#endif
