/**
 * @file    main.c
 * @brief   业务应用
 *
 * @author  xLumina
 * @version 2.0
 */

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "osal_task.h"
#include "pal_log.h"
#include "pal_i2c.h"

#include "dal_config.h"
#include "dal_pir.h"
#include "dal_relay.h"
#include "dal_storage.h"
#include "dal_audio.h"
#include "dal_touch.h"
#include "dal_display.h"
#include "dal_network.h"
#include "dal_camera.h"

#include "service_db.h"
#include "service_face_detect.h"
#include "service_face_identify.h"
#include "service_user_manage.h"

static const char *TAG = "main";

/* 系统状态枚举 */
typedef enum system_state_t {
    SYS_STATE_DEEP_SLEEP,     /**< 深度休眠态：仅 PIR 中断工作，极低功耗 */
    SYS_STATE_IDLE,           /**< 待机/缓冲态：PIR 触发后等待确认 */
    SYS_STATE_WORKING,        /**< 核心工作态：摄像头采集、AI 鉴权、UI 交互全开 */
    SYS_STATE_RELAY_OPEN,     /**< 开门执行态：继电器吸合，倒计时准备关门 */
    SYS_STATE_ERROR           /**< 故障态：外设异常或看门狗复位前的安全状态 */
} system_state_t;

typedef enum {
    DAL_INIT_STATUS_SKIP = 0,  /**< 未启用或前置条件不足，跳过初始化 */
    DAL_INIT_STATUS_OK,        /**< 初始化成功 */
    DAL_INIT_STATUS_FAIL,      /**< 初始化失败 */
} dal_init_status_t;

/**
 * @brief 将 DAL 初始化状态转换为字符串
 *
 * @param status 初始化状态
 * @return 状态字符串
 */
static const char *dal_init_status_to_str(dal_init_status_t status)
{
    switch (status) {
    case DAL_INIT_STATUS_OK:
        return "OK";
    case DAL_INIT_STATUS_FAIL:
        return "FAIL";
    case DAL_INIT_STATUS_SKIP:
    default:
        return "SKIP";
    }
}

/* ------ 硬件句柄资源 ------ */
static dal_pir_handle_t     pir_handle       = NULL;
static dal_relay_handle_t   relay_handle     = NULL;
static dal_storage_handle_t storage_handle   = NULL;
static pal_i2c_bus_handle_t board_i2c_handle = NULL;
static dal_audio_handle_t   audio_handle     = NULL;
static dal_touch_handle_t   touch_handle     = NULL;
static dal_display_handle_t display_handle   = NULL;
static dal_network_handle_t network_handle   = NULL;
static dal_camera_handle_t  camera_handle    = NULL;
static uint8_t             *camera_preview_buf = NULL;
static service_face_detect_handle_t   face_detect_handle = NULL;
static service_face_identify_handle_t face_identify_handle = NULL;


system_state_t current_state = SYS_STATE_DEEP_SLEEP;


/**
 * @brief 人体红外感应中断回调
 */
void pir_callback(dal_pir_state_t state, void *arg)
{
    (void)arg;

    switch (state) {
        case DAL_PIR_STATE_IDLE:
            current_state = SYS_STATE_DEEP_SLEEP;
            break;
        case DAL_PIR_STATE_MOTION:
            current_state = SYS_STATE_IDLE;
            break;

        default:
            break;
    }
}

/**
 * @brief 硬件初始化
 */
void hardware_init(void)
{
    int ret;
    dal_init_status_t pir_status = DAL_INIT_STATUS_FAIL;
    dal_init_status_t relay_status = DAL_INIT_STATUS_FAIL;
    dal_init_status_t i2c_status = DAL_INIT_STATUS_FAIL;
    dal_init_status_t storage_status = DAL_INIT_STATUS_FAIL;
    dal_init_status_t audio_status = DAL_INIT_STATUS_SKIP;
    dal_init_status_t touch_status = DAL_INIT_STATUS_SKIP;
    dal_init_status_t display_status = DAL_INIT_STATUS_SKIP;
    dal_init_status_t network_status = DAL_INIT_STATUS_SKIP;
    dal_init_status_t camera_status = DAL_INIT_STATUS_SKIP;

    /* 人体红外感应模块初始化 */
    const dal_pir_config_t pir_cfg = {
        .gpio_pin = PIR_INTR_PIN,
        .pull_down = true,
    };
    ret = dal_pir_init(&pir_handle, &pir_cfg);
    if (ret != 0) {
        PAL_LOGE(TAG, "人体红外感应模块初始化失败: %d", ret);
    } else {
        ret = dal_pir_set_callback(pir_handle, pir_callback, NULL);
        if (ret != 0) {
            PAL_LOGE(TAG, "人体红外感应模块中断注册失败: %d", ret);
        } else {
            ret = dal_pir_enable(pir_handle);
            if (ret != 0) {
                PAL_LOGE(TAG, "人体红外感应模块中断使能失败: %d", ret);
            } else {
                pir_status = DAL_INIT_STATUS_OK;
            }
        }
    }

    /* 继电器模块初始化 */
    const dal_relay_config_t relay_cfg = {
        .gpio_pin = RELAY_CTL_PIN,
        .active_level = 1,
        .init_open = false,
    };
    ret = dal_relay_init(&relay_handle, &relay_cfg);
    if (ret != 0) {
        PAL_LOGE(TAG, "继电器初始化失败: %d", ret);
    } else {
        relay_status = DAL_INIT_STATUS_OK;
    }

    /* 共享 I2C 总线初始化（触控 / 背光 / 音频 Codec 共用） */
    const pal_i2c_bus_config_t i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .sda_pin = BOARD_I2C_SDA_PIN,
        .scl_pin = BOARD_I2C_SCL_PIN,
        .freq_hz = BOARD_I2C_FREQ_HZ,
        .enable_internal_pullup = BOARD_I2C_ENABLE_PULLUP,
        .intr_priority = BOARD_I2C_INTR_PRIORITY,
        .trans_queue_depth = BOARD_I2C_TRANS_QUEUE_DEPTH,
    };
    ret = pal_i2c_bus_init(&board_i2c_handle, &i2c_cfg);
    if (ret != 0) {
        PAL_LOGE(TAG, "共享 I2C 总线初始化失败: %d", ret);
        board_i2c_handle = NULL;
    } else {
        i2c_status = DAL_INIT_STATUS_OK;
    }

    /* SDMMC 模块初始化 */
    const dal_storage_config_t storage_cfg = {
        .d0_pin = STORAGE_D0_PIN,
        .d1_pin = STORAGE_D1_PIN,
        .d2_pin = STORAGE_D2_PIN,
        .d3_pin = STORAGE_D3_PIN,
        .cmd_pin = STORAGE_CMD_PIN,
        .clk_pin = STORAGE_CLK_PIN,
        .cd_pin = STORAGE_CD_PIN,
        .wp_pin = STORAGE_WP_PIN,
        .format_if_mount_failed = STORAGE_FORMAT_IF_MOUNT_FAILED,
        .max_files = STORAGE_MAX_FILES,
        .freq_khz = STORAGE_FREQ_KHZ,
        .use_1bit = STORAGE_USE_1BIT,
        .mount_point = STORAGE_MOUNT_POINT,
    };
    ret = dal_storage_init(&storage_handle, &storage_cfg);
    if (ret != 0) {
        PAL_LOGE(TAG, "SD 初始化失败: %d", ret);
    } else {
        uint32_t total_mb = 0;
        uint32_t free_mb = 0;
        ret = dal_storage_get_info(storage_handle, &total_mb, &free_mb);
        if (ret == 0) {
            storage_status = DAL_INIT_STATUS_OK;
            PAL_LOGI(TAG, "SD 卡挂载成功: %s, 总容量=%lu MB, 剩余=%lu MB",
                     dal_storage_get_mount_point(storage_handle),
                     (unsigned long)total_mb,
                     (unsigned long)free_mb);
            ret = service_db_init(dal_storage_get_mount_point(storage_handle));
            if (ret != 0) {
                PAL_LOGE(TAG, "本地数据库初始化失败: %d", ret);
            }
        } else {
            PAL_LOGE(TAG, "获取 SD 卡信息失败: %d", ret);
        }
    }

    /* 音频模块初始化 */
    if (board_i2c_handle != NULL) {
        audio_status = DAL_INIT_STATUS_FAIL;
        ret = pal_i2c_dev_probe(board_i2c_handle, AUDIO_CODEC_I2C_ADDR);
        if (ret != 0) {
            PAL_LOGE(TAG, "ES8311 Codec 探测失败: %d", ret);
        } else {
            const dal_audio_config_t audio_cfg = {
                .i2c_bus_handle = board_i2c_handle,
                .i2s_port = AUDIO_I2S_PORT,
                .mclk_pin = AUDIO_MCLK_PIN,
                .sclk_pin = AUDIO_BCLK_PIN,
                .lclk_pin = AUDIO_LRCK_PIN,
                .dout_pin = AUDIO_DOUT_PIN,
                .din_pin = AUDIO_DIN_PIN,
                .pa_pin = AUDIO_PA_EN_PIN,
                .sample_rate = AUDIO_SAMPLE_RATE,
            };
            ret = dal_audio_init(&audio_handle, &audio_cfg);
            if (ret != 0) {
                PAL_LOGE(TAG, "音频模块初始化失败: %d", ret);
            } else {
                audio_status = DAL_INIT_STATUS_OK;
                PAL_LOGI(TAG, "音频模块初始化成功");
                ret = dal_audio_set_volume(audio_handle, 0xC0);
                if (ret != 0) {
                    PAL_LOGE(TAG, "音频音量设置失败: %d", ret);
                }
            }
        }
    } else {
        PAL_LOGW(TAG, "音频模块跳过：共享 I2C 总线未初始化");
    }

    /* 触摸模块初始化 */
    if (board_i2c_handle != NULL && TOUCH_H_RES > 0 && TOUCH_V_RES > 0) {
        const dal_touch_config_t touch_cfg = {
            .i2c_bus_handle = board_i2c_handle,
            .ft5406_i2c_addr = TOUCH_FT5406_I2C_ADDR,
            .h_res = TOUCH_H_RES,
            .v_res = TOUCH_V_RES,
        };
        touch_status = DAL_INIT_STATUS_FAIL;
        ret = dal_touch_init(&touch_handle, &touch_cfg);
        if (ret != 0) {
            PAL_LOGE(TAG, "触摸模块初始化失败: %d", ret);
        } else {
            touch_status = DAL_INIT_STATUS_OK;
        }
    } else {
        PAL_LOGW(TAG, "触摸模块跳过：I2C 未初始化或触摸分辨率未配置");
    }

    /* 显示模块初始化 */
    if (board_i2c_handle != NULL && DISPLAY_H_RES > 0 && DISPLAY_V_RES > 0 &&
        DISPLAY_DSI_LANE_BIT_RATE_MBPS > 0) {
        const dal_display_config_t display_cfg = {
            .i2c_bus_handle = board_i2c_handle,
            .use_attiny88 = DISPLAY_USE_ATTINY88,
            .attiny88_i2c_addr = DISPLAY_ATTINY88_I2C_ADDR,
            .default_brightness = DISPLAY_DEFAULT_BRIGHTNESS,
            .power_to_backlight_ms = DISPLAY_POWER_TO_BACKLIGHT_MS,
            .dsi_host = DISPLAY_DSI_HOST,
            .dsi_num_data_lanes = DISPLAY_DSI_NUM_DATA_LANES,
            .dsi_lane_bit_rate_mbps = DISPLAY_DSI_LANE_BIT_RATE_MBPS,
            .dsi_virtual_channel = DISPLAY_DSI_VIRTUAL_CHANNEL,
            .h_res = DISPLAY_H_RES,
            .v_res = DISPLAY_V_RES,
            .pixel_format = (pal_mipi_dsi_color_fmt_t)DISPLAY_PIXEL_FORMAT,
            .in_color_format = (pal_mipi_dsi_in_color_fmt_t)DISPLAY_IN_COLOR_FORMAT,
            .timing = {
                .h_res = DISPLAY_H_RES,
                .v_res = DISPLAY_V_RES,
                .hsync_pulse_width = DISPLAY_HSYNC_PULSE_WIDTH,
                .hsync_back_porch = DISPLAY_HSYNC_BACK_PORCH,
                .hsync_front_porch = DISPLAY_HSYNC_FRONT_PORCH,
                .vsync_pulse_width = DISPLAY_VSYNC_PULSE_WIDTH,
                .vsync_back_porch = DISPLAY_VSYNC_BACK_PORCH,
                .vsync_front_porch = DISPLAY_VSYNC_FRONT_PORCH,
                .hsync_polarity = DISPLAY_HSYNC_POLARITY,
                .vsync_polarity = DISPLAY_VSYNC_POLARITY,
            },
            .num_fbs = DISPLAY_NUM_FBS,
            .dpi_clock_freq_mhz = DISPLAY_DPI_CLOCK_FREQ_MHZ,
            .bl_use_direct_pwm = DISPLAY_BL_USE_DIRECT_PWM,
            .bl_gpio = DISPLAY_BL_GPIO,
            .bl_ledc_channel = DISPLAY_BL_LEDC_CHANNEL,
            .bl_ledc_timer = DISPLAY_BL_LEDC_TIMER,
            .bl_freq_hz = DISPLAY_BL_FREQ_HZ,
        };
        display_status = DAL_INIT_STATUS_FAIL;
        ret = dal_display_init(&display_handle, &display_cfg);
        if (ret != 0) {
            PAL_LOGE(TAG, "显示模块初始化失败: %d", ret);
        } else {
            display_status = DAL_INIT_STATUS_OK;
            (void)dal_display_on(display_handle);
            (void)dal_display_set_backlight(display_handle, DISPLAY_DEFAULT_BRIGHTNESS);
            (void)dal_display_fill(display_handle, 0, 0, DISPLAY_H_RES, DISPLAY_V_RES, 0x0000);
        }
    } else {
        PAL_LOGW(TAG, "显示模块跳过：I2C 未初始化或 DSI/面板时序未配置");
    }

    /* 网络模块初始化 */
    if (NETWORK_ETH_MDC_PIN >= 0 && NETWORK_ETH_MDIO_PIN >= 0) {
        const dal_network_config_t network_cfg = {
            .mdc_pin = NETWORK_ETH_MDC_PIN,
            .mdio_pin = NETWORK_ETH_MDIO_PIN,
            .phy_reset_pin = NETWORK_ETH_PHY_RESET_PIN,
            .phy_addr = NETWORK_ETH_PHY_ADDR,
            .phy_type = (dal_eth_phy_type_t)NETWORK_ETH_PHY_TYPE,
            .ip_cfg = {
                .use_dhcp = NETWORK_USE_DHCP,
                .static_ip = NETWORK_STATIC_IP,
                .netmask = NETWORK_NETMASK,
                .gateway = NETWORK_GATEWAY,
            },
        };
        network_status = DAL_INIT_STATUS_FAIL;
        ret = dal_network_init(&network_handle, &network_cfg);
        if (ret != 0) {
            PAL_LOGE(TAG, "网络模块初始化失败: %d", ret);
        } else {
            network_status = DAL_INIT_STATUS_OK;
        }
    } else {
        PAL_LOGW(TAG, "网络模块跳过：RMII/PHY 引脚未配置");
    }

    /* 摄像模块初始化 */
    if (board_i2c_handle != NULL) {
        const dal_camera_config_t camera_cfg = {
            .i2c_bus_handle = pal_i2c_get_bus_handle(board_i2c_handle),
            .reuse_i2c_bus = true,
            .sccb_i2c_port = CAMERA_SCCB_I2C_PORT,
            .sccb_sda_pin = CAMERA_SCCB_SDA_PIN,
            .sccb_scl_pin = CAMERA_SCCB_SCL_PIN,
            .sccb_freq_hz = CAMERA_SCCB_FREQ_HZ,
            .reset_pin = CAMERA_RESET_PIN,
            .pwdn_pin = CAMERA_PWDN_PIN,
            .device_path = CAMERA_DEVICE_PATH,
            .width = CAMERA_FRAME_WIDTH,
            .height = CAMERA_FRAME_HEIGHT,
            .pixel_format = DAL_CAMERA_PIXEL_FORMAT_RAW8,
            .buffer_count = CAMERA_BUFFER_COUNT,
            .dqbuf_timeout_ms = CAMERA_DQBUF_TIMEOUT_MS,
            .self_test_max_tries = CAMERA_SELF_TEST_MAX_TRIES,
        };

        camera_status = DAL_INIT_STATUS_FAIL;
        ret = dal_camera_init(&camera_handle, &camera_cfg);
        if (ret != 0) {
            PAL_LOGE(TAG, "摄像模块初始化失败: %d", ret);
        } else {
            ret = dal_camera_self_test(camera_handle);
            if (ret != 0) {
                PAL_LOGE(TAG, "摄像模块自检失败: %d", ret);
            } else {
                camera_status = DAL_INIT_STATUS_OK;
            }
        }
    } else {
        PAL_LOGW(TAG, "摄像模块跳过：未启用或 I2C 未初始化");
    }

    const service_face_detect_config_t detect_cfg = {
        .model_path = FACE_DETECT_MODEL_PATH,
        .model_id = FACE_DETECT_MODEL_ID,
        .score_threshold = FACE_DETECT_SCORE_THRESHOLD,
        .nms_threshold = FACE_DETECT_NMS_THRESHOLD,
        .enable_placeholder = FACE_SERVICE_PLACEHOLDER_ENABLE,
    };
    ret = service_face_detect_init(&face_detect_handle, &detect_cfg);
    if (ret != 0) {
        PAL_LOGE(TAG, "人脸检测服务初始化失败: %d", ret);
    }

    const service_face_identify_config_t identify_cfg = {
        .feature_model_path = FACE_FEATURE_MODEL_PATH,
        .model_id = FACE_FEATURE_MODEL_ID,
        .match_threshold = FACE_MATCH_THRESHOLD,
        .enable_placeholder = FACE_SERVICE_PLACEHOLDER_ENABLE,
    };
    ret = service_face_identify_init(&face_identify_handle, &identify_cfg);
    if (ret != 0) {
        PAL_LOGE(TAG, "人脸识别服务初始化失败: %d", ret);
    }

    ret = service_user_manage_init(face_detect_handle, face_identify_handle);
    if (ret != 0) {
        PAL_LOGE(TAG, "用户管理服务初始化失败: %d", ret);
    }

    service_face_detect_status_t detect_status = {0};
    service_face_identify_status_t identify_status = {0};
    (void)service_face_detect_get_status(face_detect_handle, &detect_status);
    (void)service_face_identify_get_status(face_identify_handle, &identify_status);
    PAL_LOGI(TAG, "人脸服务状态: detect_ready=%d detect_placeholder=%d identify_ready=%d identify_placeholder=%d",
             detect_status.model_ready, detect_status.placeholder,
             identify_status.model_ready, identify_status.placeholder);

    PAL_LOGI(TAG,
             "DAL 初始化汇总: PIR=%s Relay=%s I2C=%s Storage=%s Audio=%s Touch=%s Display=%s Network=%s Camera=%s",
             dal_init_status_to_str(pir_status),
             dal_init_status_to_str(relay_status),
             dal_init_status_to_str(i2c_status),
             dal_init_status_to_str(storage_status),
             dal_init_status_to_str(audio_status),
             dal_init_status_to_str(touch_status),
             dal_init_status_to_str(display_status),
             dal_init_status_to_str(network_status),
             dal_init_status_to_str(camera_status));
}

void app_main(void)
{
    /* ----- 硬件初始化 ----- */
    hardware_init();

    /* ----- 业务运行 ----- */


    while (1) {

        osal_task_delay_ms(100);
    }
}
