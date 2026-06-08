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
    /* 1. 复位 */
    ret = reg_write(d, ES8311_REG_RESET, 0x1F);
    osal_task_delay_ms(10);
    if (ret) goto fail;

    /* 2. 时钟: MCLK/4, BCLK=32fs */
    ret = reg_write(d, ES8311_REG_CLK_MGR, 0x05);
    /* 3. 上电: DAC+VREF+ADC */
    ret |= reg_write(d, ES8311_REG_SYS_PWR, 0x00);
    /* 4. I2S 格式: 16-bit, I2S Philips */
    ret |= reg_write(d, ES8311_REG_SERIAL_CTRL1, 0x00);
    /* 5. 主模式, 16-bit */
    ret |= reg_write(d, ES8311_REG_SERIAL_CTRL2, 0x02);
    /* 6. DAC 使能, 不静音 */
    ret |= reg_write(d, ES8311_REG_DAC_CTRL1, 0x18);
    ret |= reg_write(d, ES8311_REG_DAC_CTRL2, 0x02);
    /* 7. LDO/REF 电源 */
    ret |= reg_write(d, 0x10, 0x00);  /* LDO & REF */
    ret |= reg_write(d, 0x12, 0x00);  /* Analog LDO */
    /* 8. ADC/DAC routing */
    ret |= reg_write(d, 0x19, 0x02);  /* DAC_SEL = LRCK */
    ret |= reg_write(d, 0x1A, 0x30);  /* DAC_REF */
    /* 9. GPIO1=PA 使能 */
    ret |= reg_write(d, ES8311_REG_GPIO_CTRL1, 0x20);
    ret |= reg_write(d, ES8311_REG_GPIO_CTRL2, 0x01);
    /* 10. 最大音量 */
    ret |= reg_write(d, ES8311_REG_VOLUME, ES8311_VOLUME_MAX);
    if (ret) goto fail;

    /* 回读验证 */
    {
        uint8_t v[5] = {0};
        pal_i2c_read_reg(d->i2c, ES8311_REG_SYS_PWR, &v[0], 1);
        pal_i2c_read_reg(d->i2c, ES8311_REG_DAC_CTRL1, &v[1], 1);
        pal_i2c_read_reg(d->i2c, ES8311_REG_DAC_CTRL2, &v[2], 1);
        pal_i2c_read_reg(d->i2c, ES8311_REG_SERIAL_CTRL2, &v[3], 1);
        pal_i2c_read_reg(d->i2c, ES8311_REG_VOLUME, &v[4], 1);
        PAL_LOGI(TAG, "SYS=%02X D1=%02X D2=%02X S2=%02X VOL=%02X",
                 v[0], v[1], v[2], v[3], v[4]);
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
    /* DAC_CTRL2 bit2 = mute */
    uint8_t reg = 0x02; /* keep 16-bit mode */
    if (!mute) reg |= 0x02; else reg &= ~0x04;
    return reg_write(h, ES8311_REG_DAC_CTRL2, mute ? 0x00 : 0x02);
}
