/**
 * @file    attiny88.c
 * @brief   ATtiny88 背光/电源管理 MCU 驱动 - v2（寄存器 0x80-0x86）
 */

#include "attiny88.h"

#include "pal_i2c.h"
#include "pal_log.h"
#include "osal_task.h"

#include <stdlib.h>
#include <string.h>

#define TAG "ATTINY88"

/* ================================================================
 *  内部结构体
 * ================================================================ */

typedef struct attiny88_dev {
    pal_i2c_dev_handle_t i2c_dev;
    attiny88_config_t    cfg;
    bool                 inited;
} attiny88_dev_t;

/* ================================================================
 *  内部：单字节寄存器读写
 * ================================================================ */

static int write_reg(attiny88_handle_t h, uint8_t reg, uint8_t val)
{
    attiny88_dev_t *d = (attiny88_dev_t *)h;
    if (!d || !d->i2c_dev) return -1;
    uint8_t buf[2] = { reg, val };
    return pal_i2c_write(d->i2c_dev, buf, sizeof(buf));
}

static int read_reg(attiny88_handle_t h, uint8_t reg, uint8_t *val)
{
    attiny88_dev_t *d = (attiny88_dev_t *)h;
    if (!d || !d->i2c_dev || !val) return -1;
    return pal_i2c_read_reg(d->i2c_dev, reg, val, 1);
}

/* ================================================================
 *  公开 API
 * ================================================================ */

int attiny88_init(attiny88_handle_t *handle, const attiny88_config_t *cfg)
{
    if (!handle || !cfg || !cfg->i2c_dev) return -1;

    attiny88_dev_t *d = calloc(1, sizeof(*d));
    if (!d) return -1;

    d->i2c_dev = cfg->i2c_dev;
    d->inited   = false;
    memcpy(&d->cfg, cfg, sizeof(*cfg));

    /* 校验固件 ID */
    uint8_t id = 0;
    int ret = read_reg(d, ATTINY88_REG_ID, &id);
    if (ret == 0) {
        PAL_LOGI(TAG, "固件 ID: 0x%02X%s",
                 id, (id == ATTINY88_FW_ID_V2) ? " (v2)" : "");
    } else {
        PAL_LOGW(TAG, "读 ID 失败: %d，继续初始化", ret);
    }

    /* 初始状态：全部复位 */
    write_reg(d, ATTINY88_REG_PORTC, 0x00);
    write_reg(d, ATTINY88_REG_PORTB, 0x00);
    write_reg(d, ATTINY88_REG_PWM,   0x00);

    d->inited = true;
    *handle = (attiny88_handle_t)d;
    PAL_LOGI(TAG, "初始化完成");
    return 0;
}

int attiny88_deinit(attiny88_handle_t handle)
{
    attiny88_dev_t *d = (attiny88_dev_t *)handle;
    if (!d || !d->inited) return -1;
    write_reg(d, ATTINY88_REG_PORTB, 0x00);
    write_reg(d, ATTINY88_REG_PORTC, 0x00);
    free(d);
    return 0;
}

int attiny88_power_on(attiny88_handle_t handle)
{
    attiny88_dev_t *d = (attiny88_dev_t *)handle;
    if (!d || !d->inited) return -1;

    int ret;

    /* 1. 配置扫描方向 + 背光 LED 使能，桥/LCD 仍保持复位 */
    ret  = write_reg(d, ATTINY88_REG_PORTA, ATTINY88_PORTA_SCAN_LR);
    ret |= write_reg(d, ATTINY88_REG_PORTB, ATTINY88_PORTB_POWER_ON);
    ret |= write_reg(d, ATTINY88_REG_PORTC, ATTINY88_PORTC_LED_EN);
    if (ret) return ret;

    osal_task_delay_ms(d->cfg.power_on_delay_ms);

    /* 2. 设置默认亮度 */
    if (d->cfg.default_brightness > 0) {
        write_reg(d, ATTINY88_REG_PWM, d->cfg.default_brightness);
    }

    PAL_LOGI(TAG, "主电源已开启（桥复位保持中）");
    return 0;
}

int attiny88_power_off(attiny88_handle_t handle)
{
    attiny88_dev_t *d = (attiny88_dev_t *)handle;
    if (!d || !d->inited) return -1;

    write_reg(d, ATTINY88_REG_PWM,   0x00);
    write_reg(d, ATTINY88_REG_PORTC, 0x00);
    write_reg(d, ATTINY88_REG_PORTB, 0x00);

    PAL_LOGI(TAG, "电源已关闭");
    return 0;
}

/**
 * @brief 释放桥复位（供 dal_display 在 DSI 就绪后调用）
 */
int attiny88_release_bridge_reset(attiny88_handle_t handle)
{
    attiny88_dev_t *d = (attiny88_dev_t *)handle;
    if (!d || !d->inited) return -1;

    /* PORTC: LED_EN + LCD_RST(释放) + BRIDGE_RST(释放) */
    uint8_t val = ATTINY88_PORTC_LED_EN
                | ATTINY88_PORTC_LCD_RST
                | ATTINY88_PORTC_BRIDGE_RST
                | ATTINY88_PORTC_TOUCH_RST;
    int ret = write_reg(d, ATTINY88_REG_PORTC, val);
    if (ret) return ret;

    osal_task_delay_ms(d->cfg.reset_release_delay_ms);
    PAL_LOGI(TAG, "桥 + LCD 复位已释放");
    return 0;
}

int attiny88_set_brightness(attiny88_handle_t handle, uint8_t brightness)
{
    attiny88_dev_t *d = (attiny88_dev_t *)handle;
    if (!d || !d->inited) return -1;

    return write_reg(d, ATTINY88_REG_PWM, brightness);
}

int attiny88_get_brightness(attiny88_handle_t handle, uint8_t *brightness)
{
    return read_reg(handle, ATTINY88_REG_PWM, brightness);
}

int attiny88_read_id(attiny88_handle_t handle, uint8_t *id)
{
    return read_reg(handle, ATTINY88_REG_ID, id);
}

int attiny88_read_state(attiny88_handle_t handle, uint8_t *state)
{
    return read_reg(handle, ATTINY88_REG_PORTC, state);
}
