/**
 * @file    pal_mipi_csi.c
 * @brief   PAL MIPI CSI 模块 - 实现（ESP-IDF MIPI CSI 控制器封装）
 *
 * 使用 ESP-IDF esp_driver_cam 组件（esp_cam_ctlr + esp_cam_new_csi_ctlr）。
 *
 * 帧捕获流程：
 *   pal_mipi_csi_start_stream() → CSI 开始接收
 *   pal_mipi_csi_capture()      → esp_cam_ctlr_receive() 阻塞等待一帧
 *   pal_mipi_csi_stop_stream()  → CSI 停止接收
 *
 * 帧缓冲分配策略：优先使用外部提供的缓冲，否则内部分配 PSRAM DMA 缓冲。
 */

#include "pal_mipi_csi.h"

#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdlib.h>
#include <string.h>

#define TAG "PAL_CSI"

/* ================================================================
 *  内部结构体
 * ================================================================ */

typedef struct {
    esp_cam_ctlr_handle_t  ctlr;           /**< CSI 控制器句柄 */
    pal_mipi_csi_config_t  cfg;            /**< 配置副本 */
    uint8_t               *fb;             /**< 帧缓冲指针 */
    size_t                 fb_size;        /**< 帧缓冲大小 */
    bool                   fb_owned;       /**< 帧缓冲是否为 PAL 内部分配 */
    volatile bool          streaming;      /**< 流传输状态 */
    uint32_t               frame_seq;      /**< 帧序号 */
    SemaphoreHandle_t      frame_sem;      /**< 帧完成信号量 */
    pal_mipi_csi_frame_t   current_frame;  /**< 当前帧（回调填写） */
} pal_mipi_csi_internal_t;

/* ================================================================
 *  内部：PAL 颜色 → ESP-IDF color type
 * ================================================================ */

static cam_ctlr_color_t pal_to_esp_color(pal_mipi_csi_color_t color)
{
    switch (color) {
    case PAL_CSI_COLOR_RAW8:   return CAM_CTLR_COLOR_RAW8;
    case PAL_CSI_COLOR_RAW10:  return CAM_CTLR_COLOR_RAW10;
    case PAL_CSI_COLOR_RGB565: return CAM_CTLR_COLOR_RGB565;
    case PAL_CSI_COLOR_RGB888: return CAM_CTLR_COLOR_RGB888;
    case PAL_CSI_COLOR_YUV422: return CAM_CTLR_COLOR_YUV422;
    default:                   return CAM_CTLR_COLOR_RGB565;
    }
}

/** @brief 根据颜色格式计算每像素字节数 */
static size_t bytes_per_pixel(pal_mipi_csi_color_t color)
{
    switch (color) {
    case PAL_CSI_COLOR_RAW8:   return 1;
    case PAL_CSI_COLOR_RAW10:  return 2;
    case PAL_CSI_COLOR_RGB565: return 2;
    case PAL_CSI_COLOR_RGB888: return 3;
    case PAL_CSI_COLOR_YUV422: return 2;
    default:                   return 2;
    }
}

/* ================================================================
 *  CSI 事件回调（异步模式：回调中保存帧 + 释放信号量）
 * ================================================================ */

static bool csi_on_get_new_trans(esp_cam_ctlr_handle_t handle,
                                 esp_cam_ctlr_trans_t *trans,
                                 void *user_data)
{
    pal_mipi_csi_internal_t *ctx = (pal_mipi_csi_internal_t *)user_data;
    if (ctx && ctx->fb) {
        trans->buffer = ctx->fb;
        trans->buflen = ctx->fb_size;
    }
    return false;
}

static bool csi_on_trans_finished(esp_cam_ctlr_handle_t handle,
                                  esp_cam_ctlr_trans_t *trans,
                                  void *user_data)
{
    pal_mipi_csi_internal_t *ctx = (pal_mipi_csi_internal_t *)user_data;
    if (!ctx || !trans) return false;

    /* 保存帧数据到 current_frame */
    ctx->current_frame.buffer       = trans->buffer;
    ctx->current_frame.buf_len      = trans->buflen;
    ctx->current_frame.received_len = trans->received_size;
    ctx->current_frame.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ctx->current_frame.seq_num      = ctx->frame_seq++;

    /* 通知等待 capture 的任务 */
    BaseType_t higher_prio_woken = pdFALSE;
    if (ctx->frame_sem) {
        xSemaphoreGiveFromISR(ctx->frame_sem, &higher_prio_woken);
    }
    return (higher_prio_woken == pdTRUE);
}

/* ================================================================
 *  生命周期
 * ================================================================ */

int pal_mipi_csi_init(pal_mipi_csi_handle_t *handle,
                      const pal_mipi_csi_config_t *cfg)
{
    if (!handle || !cfg) return ESP_ERR_INVALID_ARG;

    pal_mipi_csi_internal_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return ESP_ERR_NO_MEM;
    memcpy(&ctx->cfg, cfg, sizeof(*cfg));

    /* ---- 0. CSI PHY LDO 供电 ---- */
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = 3,
        .voltage_mv = 2500,
    };
    esp_ldo_acquire_channel(&ldo_cfg, NULL);
    /* 忽略返回值 — 可能在 DSI 初始化时已配置 */

    /* ---- 1. 创建 CSI 控制器 ---- */
    esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id                = cfg->ctlr_id,
        .h_res                  = cfg->h_res,
        .v_res                  = cfg->v_res,
        .data_lane_num          = cfg->data_lane_num,
        .lane_bit_rate_mbps     = cfg->lane_bit_rate_mbps,
        .clk_src                = MIPI_CSI_PHY_CLK_SRC_DEFAULT,
        .input_data_color_type  = pal_to_esp_color(cfg->input_color),
        .output_data_color_type = pal_to_esp_color(cfg->output_color),
        .queue_items            = cfg->queue_items > 0 ? cfg->queue_items : 2,
        .byte_swap_en           = cfg->byte_swap_en ? 1 : 0,
        .bk_buffer_dis          = cfg->bk_buffer_dis ? 1 : 0,
    };

    esp_cam_ctlr_handle_t ctlr = NULL;
    esp_err_t ret = esp_cam_new_csi_ctlr(&csi_cfg, &ctlr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建 CSI 控制器失败: %d", ret);
        free(ctx);
        return ret;
    }

    /* ---- 2. 分配帧缓冲 ---- */
    if (cfg->allocate_fb) {
        ctx->fb_size = (size_t)cfg->h_res * cfg->v_res
                     * bytes_per_pixel(cfg->output_color);
        /* PSRAM + DMA 兼容分配（经验证 ESP32-P4 需要 MALLOC_CAP_DMA） */
        ctx->fb = heap_caps_calloc(1, ctx->fb_size,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!ctx->fb) {
            ESP_LOGE(TAG, "帧缓冲分配失败 (%u bytes)", (unsigned)ctx->fb_size);
            esp_cam_ctlr_del(ctlr);
            free(ctx);
            return ESP_ERR_NO_MEM;
        }
        ctx->fb_owned = true;
        ESP_LOGI(TAG, "帧缓冲: %p, %u bytes", ctx->fb, (unsigned)ctx->fb_size);
    } else if (cfg->external_fb && cfg->fb_size > 0) {
        ctx->fb       = (uint8_t *)cfg->external_fb;
        ctx->fb_size  = cfg->fb_size;
        ctx->fb_owned = false;
    } else {
        ESP_LOGE(TAG, "未配置帧缓冲（allocate_fb=false 且 external_fb=NULL）");
        esp_cam_ctlr_del(ctlr);
        free(ctx);
        return ESP_ERR_INVALID_ARG;
    }

    /* ---- 3. 创建帧完成信号量 ---- */
    ctx->frame_sem = xSemaphoreCreateBinary();
    if (!ctx->frame_sem) {
        if (ctx->fb_owned) free(ctx->fb);
        esp_cam_ctlr_del(ctlr);
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    /* ---- 4. 注册回调 ---- */
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans   = csi_on_get_new_trans,
        .on_trans_finished  = csi_on_trans_finished,
    };
    esp_cam_ctlr_register_event_callbacks(ctlr, &cbs, ctx);

    /* ---- 5. 使能控制器 ---- */
    ret = esp_cam_ctlr_enable(ctlr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "使能 CSI 控制器失败: %d", ret);
        vSemaphoreDelete(ctx->frame_sem);
        if (ctx->fb_owned) free(ctx->fb);
        esp_cam_ctlr_del(ctlr);
        free(ctx);
        return ret;
    }

    ctx->ctlr      = ctlr;
    ctx->streaming = false;
    ctx->frame_seq = 0;
    *handle = (pal_mipi_csi_handle_t)ctx;

    ESP_LOGI(TAG, "初始化完成 (%dx%d, %d-lane)",
             cfg->h_res, cfg->v_res, cfg->data_lane_num);
    return ESP_OK;
}

int pal_mipi_csi_deinit(pal_mipi_csi_handle_t handle)
{
    pal_mipi_csi_internal_t *ctx = (pal_mipi_csi_internal_t *)handle;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    pal_mipi_csi_stop_stream(handle);
    esp_cam_ctlr_disable(ctx->ctlr);
    esp_cam_ctlr_del(ctx->ctlr);

    if (ctx->frame_sem) vSemaphoreDelete(ctx->frame_sem);
    if (ctx->fb_owned && ctx->fb) free(ctx->fb);
    free(ctx);

    ESP_LOGI(TAG, "已释放");
    return ESP_OK;
}

/* ================================================================
 *  流控制
 * ================================================================ */

int pal_mipi_csi_start_stream(pal_mipi_csi_handle_t handle)
{
    pal_mipi_csi_internal_t *ctx = (pal_mipi_csi_internal_t *)handle;
    if (!ctx) return ESP_ERR_INVALID_ARG;
    if (ctx->streaming) return ESP_OK;

    esp_err_t ret = esp_cam_ctlr_start(ctx->ctlr);
    if (ret == ESP_OK) {
        ctx->streaming = true;
        ESP_LOGI(TAG, "流已启动");
    }
    return ret;
}

int pal_mipi_csi_stop_stream(pal_mipi_csi_handle_t handle)
{
    pal_mipi_csi_internal_t *ctx = (pal_mipi_csi_internal_t *)handle;
    if (!ctx) return ESP_ERR_INVALID_ARG;
    if (!ctx->streaming) return ESP_OK;

    esp_err_t ret = esp_cam_ctlr_stop(ctx->ctlr);
    if (ret == ESP_OK) {
        ctx->streaming = false;
        ESP_LOGI(TAG, "流已停止");
    }
    return ret;
}

/* ================================================================
 *  帧捕获（纯回调模式：等信号量 → 拷贝帧数据）
 * ================================================================ */

int pal_mipi_csi_capture(pal_mipi_csi_handle_t handle,
                         pal_mipi_csi_frame_t *frame,
                         int timeout_ms)
{
    pal_mipi_csi_internal_t *ctx = (pal_mipi_csi_internal_t *)handle;
    if (!ctx || !frame) return ESP_ERR_INVALID_ARG;
    if (!ctx->streaming) return ESP_ERR_INVALID_STATE;

    /* 等待回调 on_trans_finished 释放信号量 */
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY
                                        : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(ctx->frame_sem, ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* 拷贝回调已保存的帧数据 */
    *frame = ctx->current_frame;
    return ESP_OK;
}
