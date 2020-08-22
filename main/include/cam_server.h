/*
 * cam_server.h
 *
 *  Created on: 22 de ago de 2020
 *      Author: ceanm
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

#define CAM_SERVER_TAG "cam_server"

esp_err_t start_cam_server(void);

#ifdef __cplusplus
}
#endif
