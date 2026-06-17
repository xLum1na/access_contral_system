/**
 * @file    dal_config.h
 * @brief   DAL 设备配置文件
 *
 * @author  xLumina
 * @version 1.0
 */

#ifndef DAL_CONFIG_H
#define DAL_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----- 共享 I2C 总线配置（触控 / 背光 / 音频 Codec 共用） ----- */
#define BOARD_I2C_PORT                  0
#define BOARD_I2C_SDA_PIN               7
#define BOARD_I2C_SCL_PIN               8
#define BOARD_I2C_FREQ_HZ               400000
#define BOARD_I2C_ENABLE_PULLUP         true
#define BOARD_I2C_INTR_PRIORITY         0
#define BOARD_I2C_TRANS_QUEUE_DEPTH     0

/* ----- 人体红外感应设备配置 ----- */
#define PIR_INTR_PIN                    1

/* ----- 继电器设备配置 ----- */
#define RELAY_CTL_PIN                   2

/* ----- SD 设备配置 ----- */
#define STORAGE_D0_PIN                  39
#define STORAGE_D1_PIN                  40
#define STORAGE_D2_PIN                  41
#define STORAGE_D3_PIN                  42
#define STORAGE_CMD_PIN                 44
#define STORAGE_CLK_PIN                 43
#define STORAGE_CD_PIN                  -1
#define STORAGE_WP_PIN                  -1
#define STORAGE_FORMAT_IF_MOUNT_FAILED  false
#define STORAGE_MAX_FILES               5
#define STORAGE_FREQ_KHZ                20000
#define STORAGE_USE_1BIT                false
#define STORAGE_MOUNT_POINT             "/sdcard"

/* ----- 音频模块配置 ----- */
#define AUDIO_CODEC_I2C_ADDR            0x18
#define AUDIO_I2S_PORT                  0
#define AUDIO_MCLK_PIN                  13
#define AUDIO_BCLK_PIN                  12
#define AUDIO_LRCK_PIN                  10
#define AUDIO_DOUT_PIN                  9
#define AUDIO_DIN_PIN                   48
#define AUDIO_PA_EN_PIN                 11
#define AUDIO_SAMPLE_RATE               16000
#define AUDIO_TEST_TONE_FREQ_HZ         1000
#define AUDIO_TEST_TONE_DURATION_MS     200
#define AUDIO_TEST_TONE_AMPLITUDE       3000

/* ----- 触摸模块配置：Raspberry Pi 7\" Touch Display V1 / FT5406 ----- */
#define TOUCH_FT5406_I2C_ADDR           0x38
#define TOUCH_H_RES                     800
#define TOUCH_V_RES                     480

/* ----- 显示模块配置：Raspberry Pi 7\" Touch Display V1 / TC358762 + ATTINY88 ----- */
#define DISPLAY_USE_ATTINY88            true
#define DISPLAY_ATTINY88_I2C_ADDR       0x45
#define DISPLAY_DEFAULT_BRIGHTNESS      50
#define DISPLAY_POWER_TO_BACKLIGHT_MS   80
#define DISPLAY_DSI_HOST                0
#define DISPLAY_DSI_NUM_DATA_LANES      1
#define DISPLAY_DSI_LANE_BIT_RATE_MBPS  600
#define DISPLAY_DSI_VIRTUAL_CHANNEL     0
#define DISPLAY_H_RES                   800
#define DISPLAY_V_RES                   480
#define DISPLAY_PIXEL_FORMAT            2
#define DISPLAY_IN_COLOR_FORMAT         1
#define DISPLAY_NUM_FBS                 1
#define DISPLAY_DPI_CLOCK_FREQ_MHZ      25.98f
#define DISPLAY_HSYNC_PULSE_WIDTH       2
#define DISPLAY_HSYNC_BACK_PORCH        46
#define DISPLAY_HSYNC_FRONT_PORCH       210
#define DISPLAY_VSYNC_PULSE_WIDTH       20
#define DISPLAY_VSYNC_BACK_PORCH        4
#define DISPLAY_VSYNC_FRONT_PORCH       22
#define DISPLAY_HSYNC_POLARITY          0
#define DISPLAY_VSYNC_POLARITY          0
#define DISPLAY_BL_USE_DIRECT_PWM       false
#define DISPLAY_BL_GPIO                 -1
#define DISPLAY_BL_LEDC_CHANNEL         0
#define DISPLAY_BL_LEDC_TIMER           0
#define DISPLAY_BL_FREQ_HZ              5000

/* ----- 网络模块配置：JC-ESP32P4-M3-DEV 板载 RJ45 / IP101 PHY ----- */
#define NETWORK_ETH_MDC_PIN             31
#define NETWORK_ETH_MDIO_PIN            52
#define NETWORK_ETH_PHY_RESET_PIN       51
#define NETWORK_ETH_PHY_ADDR            1
#define NETWORK_ETH_PHY_TYPE            3
#define NETWORK_USE_DHCP                true
#define NETWORK_STATIC_IP               ""
#define NETWORK_NETMASK                 ""
#define NETWORK_GATEWAY                 ""

/* ----- 摄像头模块配置：JC-ESP32P4-M3-DEV / OV5647 / MIPI CSI ----- */
#define CAMERA_SCCB_I2C_PORT            BOARD_I2C_PORT
#define CAMERA_SCCB_SDA_PIN             BOARD_I2C_SDA_PIN
#define CAMERA_SCCB_SCL_PIN             BOARD_I2C_SCL_PIN
#define CAMERA_SCCB_FREQ_HZ             100000
#define CAMERA_RESET_PIN                -1
#define CAMERA_PWDN_PIN                 -1
#define CAMERA_DEVICE_PATH              "/dev/video0"
#define CAMERA_FRAME_WIDTH              800
#define CAMERA_FRAME_HEIGHT             800
#define CAMERA_BUFFER_COUNT             2
#define CAMERA_DQBUF_TIMEOUT_MS         2000
#define CAMERA_SELF_TEST_MAX_TRIES      3
#define CAMERA_PREVIEW_ENABLE           1
#define CAMERA_PREVIEW_INTERVAL_MS      100

/* ----- 人脸识别服务配置：模型文件开发期从 SD 卡加载 ----- */
#define FACE_DETECT_MODEL_PATH          "/sdcard/model/face_detect.espdl"
#define FACE_FEATURE_MODEL_PATH         "/sdcard/model/face_feature.espdl"
#define FACE_DETECT_MODEL_ID            0xFACE0001U
#define FACE_FEATURE_MODEL_ID           0xFACE0002U
#define FACE_DETECT_SCORE_THRESHOLD     0.50f
#define FACE_DETECT_NMS_THRESHOLD       0.45f
#define FACE_MATCH_THRESHOLD            0.72f
#define FACE_SERVICE_PLACEHOLDER_ENABLE true

#ifdef __cplusplus
}
#endif

#endif /* DAL_CONFIG_H */
