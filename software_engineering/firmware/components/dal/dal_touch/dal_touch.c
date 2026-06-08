/**
 * @file    dal_touch.c
 * @brief   DAL 触控模块 - 实现
 */

#include "dal_touch.h"

#include "ft5406.h"
#include "pal_i2c.h"
#include "pal_log.h"

#include <stdlib.h>

#define TAG "DAL_TOUCH"

typedef struct {
    ft5406_handle_t       ft5406;
    pal_i2c_dev_handle_t  i2c_dev;
    bool                  inited;
} dal_touch_internal_t;

int dal_touch_init(dal_touch_handle_t *handle, const dal_touch_config_t *cfg)
{
    if (!handle || !cfg || !cfg->i2c_bus_handle) return -1;

    dal_touch_internal_t *c = calloc(1, sizeof(*c));
    if (!c) return -1;

    pal_i2c_dev_config_t i2c_dcfg = {
        .device_address = cfg->ft5406_i2c_addr,
        .scl_speed_hz   = 400000,
    };
    int ret = pal_i2c_dev_attach(&c->i2c_dev,
                                 (pal_i2c_bus_handle_t)cfg->i2c_bus_handle,
                                 &i2c_dcfg);
    if (ret) { free(c); return ret; }

    ret = ft5406_init(&c->ft5406, c->i2c_dev, cfg->h_res, cfg->v_res);
    if (ret) {
        pal_i2c_dev_detach(c->i2c_dev);
        free(c); return ret;
    }

    c->inited = true;
    *handle = (dal_touch_handle_t)c;
    PAL_LOGI(TAG, "初始化完成");
    return 0;
}

int dal_touch_deinit(dal_touch_handle_t handle)
{
    dal_touch_internal_t *c = (dal_touch_internal_t *)handle;
    if (!c || !c->inited) return -1;
    ft5406_deinit(c->ft5406);
    pal_i2c_dev_detach(c->i2c_dev);
    free(c);
    return 0;
}

int dal_touch_read(dal_touch_handle_t handle, dal_touch_data_t *data)
{
    dal_touch_internal_t *c = (dal_touch_internal_t *)handle;
    if (!c || !c->inited || !data) return -1;

    ft5406_touch_data_t raw;
    int n = ft5406_read(c->ft5406, &raw, 0);
    if (n <= 0) return n;

    data->num_points = raw.num_points;
    for (int i = 0; i < raw.num_points; i++) {
        data->points[i].x     = raw.points[i].x;
        data->points[i].y     = raw.points[i].y;
        data->points[i].event = raw.points[i].event;
        data->points[i].id    = raw.points[i].id;
    }
    return n;
}
