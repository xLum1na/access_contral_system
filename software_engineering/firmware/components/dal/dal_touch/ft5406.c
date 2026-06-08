/**
 * @file    ft5406.c
 * @brief   FT5x06 触控控制器驱动 - 实现
 *
 * I2C 协议：8-bit 寄存器地址 + 多字节数据读取。
 * 触控数据从寄存器 0x00 开始连续存储，每点 5 字节。
 */

#include "ft5406.h"

#include "pal_i2c.h"
#include "pal_log.h"
#include "osal_task.h"

#include <stdlib.h>
#include <string.h>

#define TAG "FT5406"

/* ---- 寄存器地址 (FT5406EE8 特殊布局) ---- */
#define REG_TD_STATUS       0x02   /**< 触控点数 (FT5406EE8: 寄存器 0x02) */
#define REG_TOUCH1_XH       0x03   /**< 触控点1数据起始 (FT5406EE8: 从 0x03 开始) */
#define REG_CHIP_ID         0xA8   /**< 芯片 ID */

/* ---- 每点 6 字节 ---- */
#define TOUCH_POINT_SIZE    6

/** @brief 触控点数据寄存器偏移 */
#define REG_POINT(n)        (REG_TOUCH1_XH + (n) * TOUCH_POINT_SIZE)

/** @brief 最大一次读取的触控数据量 */
#define MAX_READ_LEN        (REG_TOUCH1_XH + FT5406_MAX_TOUCH_POINTS * TOUCH_POINT_SIZE)

/* ---- 内部结构体 ---- */
typedef struct ft5406_dev {
    pal_i2c_dev_handle_t i2c_dev;
    uint16_t             h_res;
    uint16_t             v_res;
    bool                 inited;
} ft5406_dev_t;

/* ================================================================
 *  API
 * ================================================================ */

int ft5406_init(ft5406_handle_t *handle, pal_i2c_dev_handle_t i2c_dev,
                uint16_t h_res, uint16_t v_res)
{
    if (!handle || !i2c_dev) return -1;

    ft5406_dev_t *d = calloc(1, sizeof(*d));
    if (!d) return -1;

    d->i2c_dev = i2c_dev;
    d->h_res   = h_res;
    d->v_res   = v_res;

    /* 读芯片 ID 校验 */
    uint8_t chip_id = 0;
    int ret = ft5406_read_chip_id(d, &chip_id);
    if (ret == 0) {
        PAL_LOGI(TAG, "芯片 ID: 0x%02X", chip_id);
    } else {
        PAL_LOGW(TAG, "读 ID 失败: %d", ret);
    }

    /* 唤醒 FT5406: 设置 DEVICE_MODE = 0x00 (normal) */
    uint8_t reg = 0x00, val = 0x00;
    pal_i2c_write_reg_byte(d->i2c_dev, reg, val);
    /* 设置阈值 (寄存器 0x80 = TH_GROUP, 默认 0x16) */
    pal_i2c_write_reg_byte(d->i2c_dev, 0x80, 0x16);
    /* 设置活跃周期 (寄存器 0x88 = PERIOD_ACTIVE, 默认 0x0C) */
    pal_i2c_write_reg_byte(d->i2c_dev, 0x88, 0x0E);
    osal_task_delay_ms(20);

    /* 读回确认 */
    {
        uint8_t mode = 0;
        pal_i2c_read_reg(d->i2c_dev, 0x00, &mode, 1);
        PAL_LOGI(TAG, "DEVICE_MODE = 0x%02X", mode);
    }

    d->inited = true;
    *handle = (ft5406_handle_t)d;
    PAL_LOGI(TAG, "初始化完成 (%dx%d)", h_res, v_res);
    return 0;
}

int ft5406_deinit(ft5406_handle_t handle)
{
    ft5406_dev_t *d = (ft5406_dev_t *)handle;
    if (!d || !d->inited) return -1;
    free(d);
    return 0;
}

int ft5406_read(ft5406_handle_t handle, ft5406_touch_data_t *data,
                uint32_t timeout_ms)
{
    ft5406_dev_t *d = (ft5406_dev_t *)handle;
    if (!d || !d->inited || !data) return -1;

    memset(data, 0, sizeof(*data));

    /* FT5406EE8: 读寄存器 0x02 获取触控点数 */
    uint8_t status = 0;
    int ret = pal_i2c_read_reg(d->i2c_dev, REG_TD_STATUS, &status, 1);
    if (ret) {
        PAL_LOGE(TAG, "读状态失败: %d", ret);
        return ret;
    }
    uint8_t num = status & 0x0F;

    if (num == 0) return 0;       /* 无触控 */

    if (num > FT5406_MAX_TOUCH_POINTS) num = FT5406_MAX_TOUCH_POINTS;

    /* 批量读取所有触控点数据 */
    uint8_t buf[MAX_READ_LEN] = {0};
    uint8_t reg = REG_TOUCH1_XH;
    ret = pal_i2c_write(d->i2c_dev, &reg, 1);
    if (ret) return ret;
    ret = pal_i2c_read(d->i2c_dev, buf, num * TOUCH_POINT_SIZE);
    if (ret) {
        PAL_LOGE(TAG, "读触控数据失败: %d", ret);
        return ret;
    }

    /* 解析触控点 */
    for (uint8_t i = 0; i < num; i++) {
        uint8_t *p = buf + i * TOUCH_POINT_SIZE;
        uint8_t event_id = p[0] >> 6;      /* 高2位=event */
        uint16_t x = ((uint16_t)(p[0] & 0x0F) << 8) | p[1];
        uint8_t  touch_id = p[2] >> 4;     /* 高4位=ID */
        uint16_t y = ((uint16_t)(p[2] & 0x0F) << 8) | p[3];

        /* X/Y 取反：FT5406 坐标系与 LCD 相反 */
        uint16_t rx = d->h_res - 1 - x;
        uint16_t ry = d->v_res - 1 - y;
        if (rx < d->h_res && ry < d->v_res) {
            data->points[i].x     = rx;
            data->points[i].y     = ry;
            data->points[i].event = event_id;
            data->points[i].id    = touch_id;
            data->num_points++;
        }
    }

    return data->num_points;
}

int ft5406_read_chip_id(ft5406_handle_t handle, uint8_t *id)
{
    ft5406_dev_t *d = (ft5406_dev_t *)handle;
    if (!d || !d->i2c_dev || !id) return -1;
    return pal_i2c_read_reg(d->i2c_dev, REG_CHIP_ID, id, 1);
}
