/**
 * @file    dal_camera.c
 * @brief   DAL 摄像头模块 - 实现（统一摄像头抽象层）
 *
 * 初始化流程：
 *   1. IMX219 传感器 I2C 设备挂载
 *   2. IMX219 传感器初始化（读 ID → PLL → 分辨率 → streaming）
 *   3. MIPI CSI 控制器初始化 + 帧缓冲分配
 *   4. CSI 数据流启动
 */

#include "dal_camera.h"

#include "pal_mipi_csi.h"
#include "pal_i2c.h"
#include "pal_log.h"

#include <stdlib.h>
#include <string.h>

#define TAG "DAL_CAMERA"

/* ================================================================
 *  内部结构体
 * ================================================================ */

typedef struct {
    imx219_handle_t       sensor;        /**< IMX219 传感器句柄 */
    pal_mipi_csi_handle_t csi;           /**< MIPI CSI 主机句柄 */
    imx219_resolution_t   resolution;    /**< 当前分辨率 */
    bool                  inited;
    bool                  streaming;
    pal_i2c_dev_handle_t  sensor_i2c_dev; /**< IMX219 I2C 设备句柄 */
} dal_camera_internal_t;

/* ================================================================
 *  API
 * ================================================================ */

int dal_camera_init(dal_camera_handle_t *handle,
                    const dal_camera_config_t *cfg)
{
    if (!handle || !cfg || !cfg->i2c_bus_handle) return -1;

    dal_camera_internal_t *c = calloc(1, sizeof(*c));
    if (!c) return -1;
    int ret;

    /* ---- 1. 挂载 IMX219 I2C 设备 ---- */
    pal_i2c_dev_config_t i2c_dcfg = {
        .device_address = cfg->imx219_i2c_addr,
        .scl_speed_hz   = 400000,
    };
    ret = pal_i2c_dev_attach(&c->sensor_i2c_dev,
                             (pal_i2c_bus_handle_t)cfg->i2c_bus_handle,
                             &i2c_dcfg);
    if (ret) {
        PAL_LOGE(TAG, "IMX219 I2C 挂载失败: %d", ret);
        free(c); return ret;
    }

    /* ---- 2. 初始化 IMX219 传感器 ---- */
    imx219_config_t sensor_cfg = {
        .i2c_dev         = c->sensor_i2c_dev,
        .resolution      = cfg->resolution,
        .fps             = cfg->fps,
        .flip_horizontal = cfg->flip_horizontal,
        .flip_vertical   = cfg->flip_vertical,
        .xclk_pin        = cfg->xclk_pin,
    };
    ret = imx219_init(&c->sensor, &sensor_cfg);
    if (ret) {
        PAL_LOGE(TAG, "IMX219 初始化失败: %d", ret);
        pal_i2c_dev_detach(c->sensor_i2c_dev);
        free(c); return ret;
    }

    /* ---- 3. 获取分辨率 ---- */
    imx219_res_info_t info = imx219_res_info(cfg->resolution);

    /* ---- 4. 初始化 MIPI CSI 控制器 ---- */
    pal_mipi_csi_config_t csi_cfg = {
        .ctlr_id            = cfg->csi_ctlr_id,
        .h_res              = info.width,
        .v_res              = info.height,
        .data_lane_num      = cfg->csi_data_lane_num,
        .lane_bit_rate_mbps = cfg->csi_lane_bit_rate_mbps,
        .input_color        = cfg->input_color,
        .output_color       = cfg->output_color,
        .queue_items        = cfg->csi_queue_items,
        .byte_swap_en       = false,
        .bk_buffer_dis      = false,
        .allocate_fb        = cfg->allocate_fb,
        .fb_size            = 0,  /* 自动计算 */
        .external_fb        = NULL,
        .frame_cb           = NULL,
        .frame_cb_arg       = NULL,
    };
    ret = pal_mipi_csi_init(&c->csi, &csi_cfg);
    if (ret) {
        PAL_LOGE(TAG, "CSI 初始化失败: %d", ret);
        imx219_deinit(c->sensor);
        pal_i2c_dev_detach(c->sensor_i2c_dev);
        free(c); return ret;
    }

    /* ---- 5. 启动 CSI 数据流 ---- */
    ret = pal_mipi_csi_start_stream(c->csi);
    if (ret) {
        PAL_LOGE(TAG, "CSI 启动失败: %d", ret);
        pal_mipi_csi_deinit(c->csi);
        imx219_deinit(c->sensor);
        pal_i2c_dev_detach(c->sensor_i2c_dev);
        free(c); return ret;
    }
    c->streaming = true;

    c->resolution = cfg->resolution;
    c->inited     = true;
    *handle = (dal_camera_handle_t)c;

    PAL_LOGI(TAG, "初始化完成 (%dx%d)", info.width, info.height);
    return 0;
}

int dal_camera_deinit(dal_camera_handle_t handle)
{
    dal_camera_internal_t *c = (dal_camera_internal_t *)handle;
    if (!c || !c->inited) return -1;

    if (c->csi)    pal_mipi_csi_deinit(c->csi);
    if (c->sensor) imx219_deinit(c->sensor);
    if (c->sensor_i2c_dev) pal_i2c_dev_detach(c->sensor_i2c_dev);

    free(c);
    PAL_LOGI(TAG, "已释放");
    return 0;
}

/* ---- 帧捕获 ---- */

int dal_camera_capture(dal_camera_handle_t handle,
                       pal_mipi_csi_frame_t *frame,
                       int timeout_ms)
{
    dal_camera_internal_t *c = (dal_camera_internal_t *)handle;
    if (!c || !c->inited || !c->streaming) return -1;
    return pal_mipi_csi_capture(c->csi, frame, timeout_ms);
}

/* ---- 传感器控制 ---- */

int dal_camera_set_exposure(dal_camera_handle_t handle,
                            uint32_t exposure_lines)
{
    dal_camera_internal_t *c = (dal_camera_internal_t *)handle;
    if (!c || !c->inited || !c->sensor) return -1;
    return imx219_set_exposure(c->sensor, exposure_lines);
}

int dal_camera_set_gain(dal_camera_handle_t handle, uint16_t gain)
{
    dal_camera_internal_t *c = (dal_camera_internal_t *)handle;
    if (!c || !c->inited || !c->sensor) return -1;
    return imx219_set_gain(c->sensor, gain);
}

int dal_camera_get_resolution(dal_camera_handle_t handle,
                              uint16_t *w, uint16_t *h)
{
    dal_camera_internal_t *c = (dal_camera_internal_t *)handle;
    if (!c || !c->inited || !w || !h) return -1;
    imx219_res_info_t info = imx219_res_info(c->resolution);
    *w = info.width;
    *h = info.height;
    return 0;
}
