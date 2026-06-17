/**
 * @file    es8311.c
 * @brief   ES8311 最小化驱动 - I2C 寄存器初始化序列
 */
#include "es8311.h"
#include "pal_i2c.h"
#include "pal_log.h"
#include "osal_task.h"
#include <stdlib.h>

#define TAG "ES8311"

typedef struct es8311_dev { pal_i2c_dev_handle_t i2c; bool inited; } es8311_dev_t;

static int reg_write(es8311_handle_t h, uint8_t reg, uint8_t val) {
    es8311_dev_t *d = (es8311_dev_t *)h;
    uint8_t buf[2] = {reg, val};
    return pal_i2c_write(d->i2c, buf, 2);
}

int es8311_init(es8311_handle_t *handle, pal_i2c_dev_handle_t i2c_dev)
{
    if (!handle || !i2c_dev) return -1;
    es8311_dev_t *d = calloc(1, sizeof(*d));
    if (!d) { return -1; }
    d->i2c = i2c_dev;

    int ret;

    /* 1. 复位并进入 I2S slave 模式 */
    ret = reg_write(d, ES8311_REG_SYS0D, 0xFA);
    ret |= reg_write(d, ES8311_REG_GPIO44, 0x08);
    ret |= reg_write(d, ES8311_REG_GPIO44, 0x08);
    ret |= reg_write(d, ES8311_REG_CLK_MGR1, 0x30);
    ret |= reg_write(d, ES8311_REG_CLK_MGR2, 0x00);
    ret |= reg_write(d, ES8311_REG_CLK_MGR3, 0x10);
    ret |= reg_write(d, ES8311_REG_ADC16, 0x24);
    ret |= reg_write(d, ES8311_REG_CLK_MGR4, 0x10);
    ret |= reg_write(d, ES8311_REG_CLK_MGR5, 0x00);
    ret |= reg_write(d, ES8311_REG_SYS0B, 0x00);
    ret |= reg_write(d, ES8311_REG_SYS0C, 0x00);
    ret |= reg_write(d, ES8311_REG_SYS10, 0x1F);
    ret |= reg_write(d, ES8311_REG_SYS11, 0x7F);
    ret |= reg_write(d, ES8311_REG_RESET, 0x80); /* slave mode */
    ret |= reg_write(d, ES8311_REG_CLK_MGR1, 0x3F); /* 使用外部 MCLK */
    ret |= reg_write(d, ES8311_REG_CLK_MGR6, 0x00);
    ret |= reg_write(d, ES8311_REG_SYS13, 0x10);
    ret |= reg_write(d, ES8311_REG_ADC1B, 0x0A);
    ret |= reg_write(d, ES8311_REG_ADC1C, 0x6A);
    ret |= reg_write(d, ES8311_REG_GPIO44, 0x58); /* 内部参考 */

    /* 2. 16 kHz / MCLK=256fs(4.096 MHz) / 16-bit I2S Philips */
    ret |= reg_write(d, ES8311_REG_SDP_IN, 0x0C);
    ret |= reg_write(d, ES8311_REG_SDP_OUT, 0x0C);
    ret |= reg_write(d, ES8311_REG_CLK_MGR2, 0x00);
    ret |= reg_write(d, ES8311_REG_CLK_MGR5, 0x00);
    ret |= reg_write(d, ES8311_REG_CLK_MGR3, 0x10);
    ret |= reg_write(d, ES8311_REG_CLK_MGR4, 0x20);
    ret |= reg_write(d, ES8311_REG_CLK_MGR7, 0x00);
    ret |= reg_write(d, ES8311_REG_CLK_MGR8, 0xFF);
    ret |= reg_write(d, ES8311_REG_CLK_MGR6, 0x03);

    /* 3. 启用 DAC 输出并取消静音 */
    ret |= reg_write(d, ES8311_REG_ADC17, 0xBF);
    ret |= reg_write(d, ES8311_REG_SYS0E, 0x02);
    ret |= reg_write(d, ES8311_REG_SYS12, 0x00);
    ret |= reg_write(d, ES8311_REG_SYS14, 0x1A);
    ret |= reg_write(d, ES8311_REG_SYS0D, 0x01);
    ret |= reg_write(d, ES8311_REG_ADC15, 0x40);
    ret |= reg_write(d, ES8311_REG_DAC37, 0x08);
    ret |= reg_write(d, ES8311_REG_GP45, 0x00);
    ret |= reg_write(d, ES8311_REG_DAC31, 0x00);
    ret |= reg_write(d, ES8311_REG_VOLUME, ES8311_VOLUME_MAX);
    if (ret) goto fail;

    osal_task_delay_ms(10);

    /* 回读验证 */
    {
        uint8_t v[6] = {0};
        pal_i2c_read_reg(d->i2c, ES8311_REG_RESET, &v[0], 1);
        pal_i2c_read_reg(d->i2c, ES8311_REG_CLK_MGR1, &v[1], 1);
        pal_i2c_read_reg(d->i2c, ES8311_REG_SDP_IN, &v[2], 1);
        pal_i2c_read_reg(d->i2c, ES8311_REG_SYS12, &v[3], 1);
        pal_i2c_read_reg(d->i2c, ES8311_REG_DAC31, &v[4], 1);
        pal_i2c_read_reg(d->i2c, ES8311_REG_VOLUME, &v[5], 1);
        PAL_LOGI(TAG, "RST=%02X CLK1=%02X DIN=%02X DAC=%02X MUTE=%02X VOL=%02X",
                 v[0], v[1], v[2], v[3], v[4], v[5]);
    }

    d->inited = true; *handle = (es8311_handle_t)d;
    PAL_LOGI(TAG, "初始化完成"); return 0;
fail:
    PAL_LOGE(TAG, "init fail"); free(d); return -1;
}

int es8311_deinit(es8311_handle_t h) {
    es8311_dev_t *d = (es8311_dev_t *)h;
    if (!d || !d->inited) return -1;
    reg_write(d, ES8311_REG_RESET, 0x00); free(d); return 0;
}

int es8311_set_volume(es8311_handle_t h, uint8_t vol) {
    return reg_write(h, ES8311_REG_VOLUME, vol);
}

int es8311_mute(es8311_handle_t h, bool mute) {
    return reg_write(h, ES8311_REG_DAC31, mute ? 0x60 : 0x00);
}
