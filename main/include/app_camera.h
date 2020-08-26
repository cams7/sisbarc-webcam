/*
 * app_camera.h
 *
 *  Created on: 22 de ago de 2020
 *      Author: ceanm
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "sensor.h"

#define CAM_BOARD         "AI-THINKER"
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define MIN_FRAMESIZE FRAMESIZE_96X96 //Resolution
#define MAX_FRAMESIZE FRAMESIZE_UXGA
#define MIN_QUALITY 4 //Quality
#define MAX_QUALITY 63
#define MIN_BRIGHTNESS -2 //Brightness
#define MAX_BRIGHTNESS 2
#define MIN_CONTRAST -2 //Contrast
#define MAX_CONTRAST 2
#define MIN_SATURATION -2 //Saturation
#define MAX_SATURATION 2
#define MIN_SHARPNESS -2
#define MAX_SHARPNESS 2
#define MIN_SPECIAL_EFFECT 0 //Special Effect
#define MAX_SPECIAL_EFFECT 6
#define MIN_WB_MODE 0 //WB Mode
#define MAX_WB_MODE 4
#define MIN_AE_LEVEL -2 //AE Level
#define MAX_AE_LEVEL 2
#define MIN_AEC_VALUE 0 //Exposure
#define MAX_AEC_VALUE 1200
#define MIN_AGC_GAIN 0 //Gain
#define MAX_AGC_GAIN 30
#define MIN_GAINCEILING GAINCEILING_2X //Gain Ceiling
#define MAX_GAINCEILING GAINCEILING_128X

#define MIN_XCLK_MHZ 1 //XCLK MHz
#define MAX_XCLK_MHZ 20

#define MIN_RESOLUTION_START_X 0 //Start X
#define MAX_RESOLUTION_START_X 2

#define APP_CAMERA_TAG "app_camera"

esp_err_t init_camera(void);

#ifdef __cplusplus
}
#endif
