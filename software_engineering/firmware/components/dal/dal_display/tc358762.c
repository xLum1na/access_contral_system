/**
 * @file    tc358762.c
 * @brief   TC358762 桥接器驱动 - v2（DSI Generic Write 实现）
 *
 * TC358762 寄存器通过 DSI Generic Long Write (DT=0x29) 6 字节包配置：
 *   payload[6] = { reg_lo, reg_hi, val_b0, val_b1, val_b2, val_b3 }
 */

#include "tc358762.h"

#include "pal_mipi_dsi.h"
#include "pal_log.h"

#include <stdlib.h>
#include <string.h>

#define TAG "TC358762"

/* ================================================================
 *  内部结构体
 * ================================================================ */

typedef struct tc358762_dev {
    pal_mipi_dsi_handle_t dsi;
    uint8_t               vc;
    bool                  inited;
} tc358762_dev_t;

/* ================================================================
 *  内部：16-bit 寄存器写入（DSI Generic Long Write）
 * ================================================================ */

/**
 * @brief 通过 DSI Generic Long Write 写 32-bit 值到 16-bit 地址寄存器
 *
 * 包格式 (6 字节 payload)：
 *   [0] = reg_addr_lo
 *   [1] = reg_addr_hi
 *   [2] = val_b0 (LSB)
 *   [3] = val_b1
 *   [4] = val_b2
 *   [5] = val_b3 (MSB)
 */
static int tc358762_write_reg(tc358762_handle_t handle,
                              uint16_t addr, uint32_t val)
{
    tc358762_dev_t *d = (tc358762_dev_t *)handle;
    if (!d || !d->dsi) return -1;

    uint8_t payload[6] = {
        (uint8_t)((addr >> 0) & 0xFF),
        (uint8_t)((addr >> 8) & 0xFF),
        (uint8_t)((val  >> 0)  & 0xFF),
        (uint8_t)((val  >> 8)  & 0xFF),
        (uint8_t)((val  >> 16) & 0xFF),
        (uint8_t)((val  >> 24) & 0xFF),
    };
    return pal_mipi_dsi_send_generic_write(d->dsi, d->vc, payload, 6);
}

/* ================================================================
 *  公开 API
 * ================================================================ */

int tc358762_init(tc358762_handle_t *handle, const tc358762_config_t *cfg)
{
    if (!handle || !cfg || !cfg->dsi_handle) return -1;

    tc358762_dev_t *d = calloc(1, sizeof(*d));
    if (!d) return -1;
    d->dsi    = cfg->dsi_handle;
    d->vc     = cfg->vc;
    d->inited = false;

    const tc358762_timing_t *t = &cfg->timing;
    int ret;

    /* ---- DSI 通道配置：使能 Clock + D0 ---- */
    ret = tc358762_write_reg(d, TC358762_REG_DSI_LANEENABLE,
                             DSI_LANEENABLE_CLOCK | DSI_LANEENABLE_D0);
    if (ret) goto fail;

    /* ---- PPI 配置 ---- */
    ret  = tc358762_write_reg(d, TC358762_REG_CLRSIPOCOUNT, 0x05);
    ret |= tc358762_write_reg(d, TC358762_REG_ATMR,          0x00);
    ret |= tc358762_write_reg(d, TC358762_REG_LPTXTIMECNT,   0x04);
    if (ret) goto fail;

    /* ---- LCD 控制 ---- */
    ret = tc358762_write_reg(d, TC358762_REG_LCDCTRL, 0x00100150);
    if (ret) goto fail;

    /* ---- 系统控制 ---- */
    ret = tc358762_write_reg(d, TC358762_REG_SYSCTRL, 0x040F);
    if (ret) goto fail;

    /* ---- 时序寄存器 ---- */
    /* HS_HBP: hsync_width << 16 | hsync_width + hback_porch */
    ret  = tc358762_write_reg(d, TC358762_REG_HS_HBP,
                              ((uint32_t)t->hsync_pulse_width << 16)
                            | (t->hsync_pulse_width + t->hsync_back_porch));
    /* HDISP_HFP: hsync_width + hback_porch + h_res << 16 | ... + hfront_porch */
    ret |= tc358762_write_reg(d, TC358762_REG_HDISP_HFP,
                              ((uint32_t)(t->hsync_pulse_width + t->hsync_back_porch + t->h_res) << 16)
                            | (t->hsync_pulse_width + t->hsync_back_porch + t->h_res + t->hsync_front_porch));
    /* VS_VBP: vsync_width << 16 | vsync_width + vback_porch */
    ret |= tc358762_write_reg(d, TC358762_REG_VS_VBP,
                              ((uint32_t)t->vsync_pulse_width << 16)
                            | (t->vsync_pulse_width + t->vsync_back_porch));
    /* VDISP_VFP: vsync_width + vback_porch + v_res << 16 | ... + vfront_porch */
    ret |= tc358762_write_reg(d, TC358762_REG_VDISP_VFP,
                              ((uint32_t)(t->vsync_pulse_width + t->vsync_back_porch + t->v_res) << 16)
                            | (t->vsync_pulse_width + t->vsync_back_porch + t->v_res + t->vsync_front_porch));
    if (ret) goto fail;

    /* ---- 启动桥接 ---- */
    ret  = tc358762_write_reg(d, TC358762_REG_PPI_STARTPPI, 1);
    ret |= tc358762_write_reg(d, TC358762_REG_DSI_STARTDSI, 1);
    if (ret) goto fail;

    d->inited = true;
    *handle = (tc358762_handle_t)d;
    PAL_LOGI(TAG, "桥初始化完成 (%dx%d)", t->h_res, t->v_res);
    return 0;

fail:
    PAL_LOGE(TAG, "初始化失败: %d", ret);
    free(d);
    return ret;
}

int tc358762_deinit(tc358762_handle_t handle)
{
    tc358762_dev_t *d = (tc358762_dev_t *)handle;
    if (!d || !d->inited) return -1;
    /* 停桥接 */
    tc358762_write_reg(d, TC358762_REG_SYSCTRL, 0x0400);
    free(d);
    return 0;
}
