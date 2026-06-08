/**
 * @file    dal_display.c
 * @brief   DAL 显示模块 - 统一显示抽象层（v2）
 *
 * 初始化流程（严格顺序）：
 *   1. I2C 初始化（外部完成）
 *   2. ATtiny88 主电源上电（桥保持复位）
 *   3. 创建 MIPI DSI 总线 + DPI 面板 + 启动视频输出
 *   4. ATtiny88 释放桥复位
 *   5. TC358762 桥寄存器初始化（通过 DSI Generic Write）
 *   6. 背光点亮
 *
 * 参考：树莓派 7" DSI 屏验证文档
 */

#include "dal_display.h"

#include "tc358762.h"
#include "attiny88.h"
#include "pal_mipi_dsi.h"
#include "pal_i2c.h"
#include "pal_ledc.h"
#include "pal_log.h"
#include "osal_task.h"

#include <stdlib.h>
#include <string.h>

#define TAG "DAL_DISPLAY"

/* ================================================================
 *  内部结构体
 * ================================================================ */

typedef struct {
    pal_mipi_dsi_handle_t  dsi;        /**< MIPI DSI 句柄 */
    tc358762_handle_t      bridge;     /**< TC358762 桥接器 */
    attiny88_handle_t      backlight;  /**< ATtiny88 背光 */
    bool                   inited;
    bool                   display_on;
    uint8_t                brightness_pct;
} dal_display_internal_t;

static uint8_t pct_to_8bit(uint8_t p) { return (p >= 100) ? 255 : (uint8_t)(p * 255 / 100); }

/* ================================================================
 *  API
 * ================================================================ */

int dal_display_init(dal_display_handle_t *handle,
                     const dal_display_config_t *cfg)
{
    if (!handle || !cfg || !cfg->i2c_bus_handle) return -1;

    dal_display_internal_t *c = calloc(1, sizeof(*c));
    if (!c) return -1;
    int ret;

    /* ============================================================
     *  第 1 步：ATtiny88 上电（桥保持复位）
     * ============================================================ */
    if (cfg->use_attiny88) {
        pal_i2c_dev_handle_t attiny_i2c = NULL;
        pal_i2c_dev_config_t i2c_dcfg = {
            .device_address = cfg->attiny88_i2c_addr,
            .scl_speed_hz   = 400000,
        };
        ret = pal_i2c_dev_attach(&attiny_i2c,
                                 (pal_i2c_bus_handle_t)cfg->i2c_bus_handle,
                                 &i2c_dcfg);
        if (ret) {
            PAL_LOGW(TAG, "ATtiny88 I2C 挂载失败: %d", ret);
        } else {
            attiny88_config_t acfg = {
                .i2c_dev               = attiny_i2c,
                .power_on_delay_ms     = cfg->power_to_backlight_ms,
                .reset_release_delay_ms = 100,
                .default_brightness    = pct_to_8bit(cfg->default_brightness),
            };
            ret = attiny88_init(&c->backlight, &acfg);
            if (ret) {
                PAL_LOGW(TAG, "ATtiny88 初始化失败: %d", ret);
                pal_i2c_dev_detach(attiny_i2c);
                c->backlight = NULL;
            } else {
                /* 主电源上电，桥仍保持复位 */
                attiny88_power_on(c->backlight);
            }
        }
    }

    /* ============================================================
     *  第 2 步：创建 MIPI DSI 总线 + DPI 面板
     * ============================================================ */
    pal_mipi_dsi_config_t dsi_cfg = {
        .dsi_host            = cfg->dsi_host,
        .num_data_lanes      = cfg->dsi_num_data_lanes,
        .lane_bit_rate_mbps  = cfg->dsi_lane_bit_rate_mbps,
        .virtual_channel     = cfg->dsi_virtual_channel,
        .h_res               = cfg->h_res,
        .v_res               = cfg->v_res,
        .pixel_format        = cfg->pixel_format,
        .in_color_format     = cfg->in_color_format,
        .timing              = cfg->timing,
        .num_fbs             = cfg->num_fbs,
        .use_dma2d           = false,
        .dpi_clock_freq_mhz  = cfg->dpi_clock_freq_mhz,
        .bl_enabled          = false,
    };

    ret = pal_mipi_dsi_init(&c->dsi, &dsi_cfg);
    if (ret) { PAL_LOGE(TAG, "DSI 初始化失败: %d", ret); goto fail; }
    PAL_LOGI(TAG, "DSI 总线 + DPI 面板已创建");

    /* ============================================================
     *  第 3 步：释放桥复位 + 初始化 TC358762 寄存器
     * ============================================================ */
    if (c->backlight) {
        attiny88_release_bridge_reset(c->backlight);
    }

    tc358762_config_t bridge_cfg = {
        .dsi_handle = c->dsi,
        .vc         = (uint8_t)cfg->dsi_virtual_channel,
        .timing = {
            .h_res             = cfg->timing.h_res,
            .v_res             = cfg->timing.v_res,
            .hsync_pulse_width = cfg->timing.hsync_pulse_width,
            .hsync_back_porch  = cfg->timing.hsync_back_porch,
            .hsync_front_porch = cfg->timing.hsync_front_porch,
            .vsync_pulse_width = cfg->timing.vsync_pulse_width,
            .vsync_back_porch  = cfg->timing.vsync_back_porch,
            .vsync_front_porch = cfg->timing.vsync_front_porch,
        },
    };
    ret = tc358762_init(&c->bridge, &bridge_cfg);
    if (ret) { PAL_LOGE(TAG, "TC358762 初始化失败: %d", ret); goto fail; }

    /* ============================================================
     *  完成
     * ============================================================ */
    c->inited         = true;
    c->brightness_pct = cfg->default_brightness;
    *handle = (dal_display_handle_t)c;
    PAL_LOGI(TAG, "显示子系统初始化完成 (%dx%d)", cfg->h_res, cfg->v_res);
    return 0;

fail:
    if (c->dsi)      pal_mipi_dsi_deinit(c->dsi);
    if (c->backlight) attiny88_deinit(c->backlight);
    free(c);
    return ret;
}

int dal_display_deinit(dal_display_handle_t handle)
{
    dal_display_internal_t *c = (dal_display_internal_t *)handle;
    if (!c || !c->inited) return -1;

    dal_display_off(handle);
    if (c->bridge)    tc358762_deinit(c->bridge);
    if (c->dsi)       pal_mipi_dsi_deinit(c->dsi);
    if (c->backlight) attiny88_deinit(c->backlight);
    free(c);
    PAL_LOGI(TAG, "已释放");
    return 0;
}

/* ---- 显示控制 ---- */

int dal_display_on(dal_display_handle_t handle)
{
    dal_display_internal_t *c = (dal_display_internal_t *)handle;
    if (!c || !c->inited) return -1;
    if (c->display_on) return 0;

    pal_mipi_dsi_display_on(c->dsi);
    c->display_on = true;

    if (c->brightness_pct > 0)
        dal_display_set_backlight(handle, c->brightness_pct);

    PAL_LOGI(TAG, "显示已开启");
    return 0;
}

int dal_display_off(dal_display_handle_t handle)
{
    dal_display_internal_t *c = (dal_display_internal_t *)handle;
    if (!c || !c->inited) return -1;
    if (!c->display_on) return 0;

    if (c->backlight) attiny88_set_brightness(c->backlight, 0);
    pal_mipi_dsi_display_off(c->dsi);
    c->display_on = false;
    PAL_LOGI(TAG, "显示已关闭");
    return 0;
}

/* ---- 绘制（透传 PAL） ---- */

int dal_display_draw_bitmap(dal_display_handle_t h,
                            uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h2, const void *d)
{
    dal_display_internal_t *c = (dal_display_internal_t *)h;
    if (!c || !c->inited || !c->dsi) return -1;
    return pal_mipi_dsi_draw_bitmap(c->dsi, x, y, w, h2, d);
}

int dal_display_fill(dal_display_handle_t h,
                     uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h2, uint32_t color)
{
    dal_display_internal_t *c = (dal_display_internal_t *)h;
    if (!c || !c->inited || !c->dsi) return -1;
    return pal_mipi_dsi_fill(c->dsi, x, y, w, h2, color);
}

int dal_display_get_fb(dal_display_handle_t h, void **fb0, void **fb1)
{
    dal_display_internal_t *c = (dal_display_internal_t *)h;
    if (!c || !c->inited || !c->dsi) return -1;
    return pal_mipi_dsi_get_fb(c->dsi, fb0, fb1);
}

/* ---- 背光 ---- */

int dal_display_set_backlight(dal_display_handle_t handle, uint8_t pct)
{
    dal_display_internal_t *c = (dal_display_internal_t *)handle;
    if (!c || !c->inited) return -1;
    if (pct > 100) pct = 100;
    c->brightness_pct = pct;

    if (c->backlight)
        return attiny88_set_brightness(c->backlight, pct_to_8bit(pct));
    return 0;
}
