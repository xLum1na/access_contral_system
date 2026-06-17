/**
 * @file    service_touch.c
 * @brief   触摸服务实现
 */

#include "service_touch.h"

#include <stdlib.h>
#include <string.h>

static bool s_inited;
static dal_touch_handle_t s_touch;
static service_touch_config_t s_cfg;
static service_touch_status_t s_status;
static service_touch_event_cb_t s_cb;
static void *s_cb_arg;
static bool s_pressed;
static service_touch_point_t s_down_point;

static void touch_default_config(service_touch_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->h_res = 800;
    cfg->v_res = 480;
    cfg->rotate = SERVICE_TOUCH_ROTATE_0;
    cfg->long_press_ms = 800;
    cfg->swipe_threshold_px = 50;
}

static uint16_t clamp_u16(int value, uint16_t max_value)
{
    if (value < 0) return 0;
    if (max_value == 0) return 0;
    if (value >= (int)max_value) return (uint16_t)(max_value - 1U);
    return (uint16_t)value;
}

static service_touch_point_t transform_point(uint16_t x, uint16_t y)
{
    int tx = x;
    int ty = y;
    int nx = tx;
    int ny = ty;

    switch (s_cfg.rotate) {
    case SERVICE_TOUCH_ROTATE_90:
        nx = (int)s_cfg.v_res - 1 - ty;
        ny = tx;
        break;
    case SERVICE_TOUCH_ROTATE_180:
        nx = (int)s_cfg.h_res - 1 - tx;
        ny = (int)s_cfg.v_res - 1 - ty;
        break;
    case SERVICE_TOUCH_ROTATE_270:
        nx = ty;
        ny = (int)s_cfg.h_res - 1 - tx;
        break;
    case SERVICE_TOUCH_ROTATE_0:
    default:
        break;
    }

    if (s_cfg.mirror_x) nx = (int)s_cfg.h_res - 1 - nx;
    if (s_cfg.mirror_y) ny = (int)s_cfg.v_res - 1 - ny;

    service_touch_point_t point = {
        .x = clamp_u16(nx, s_cfg.h_res),
        .y = clamp_u16(ny, s_cfg.v_res),
        .pressed = true,
    };
    return point;
}

static void emit_event(service_touch_event_type_t type, service_touch_point_t point)
{
    service_touch_event_t event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.point = point;
    s_status.last_point = point;
    s_status.event_count++;
    if (s_cb) {
        s_cb(&event, s_cb_arg);
    }
}

int service_touch_init(dal_touch_handle_t touch,
                       const service_touch_config_t *cfg)
{
    if (!touch) return SERVICE_TOUCH_ERR_PARAM;
    memset(&s_status, 0, sizeof(s_status));
    touch_default_config(&s_cfg);
    if (cfg) {
        s_cfg = *cfg;
        if (s_cfg.h_res == 0) s_cfg.h_res = 800;
        if (s_cfg.v_res == 0) s_cfg.v_res = 480;
        if (s_cfg.long_press_ms == 0) s_cfg.long_press_ms = 800;
        if (s_cfg.swipe_threshold_px == 0) s_cfg.swipe_threshold_px = 50;
    }
    s_touch = touch;
    s_pressed = false;
    s_cb = NULL;
    s_cb_arg = NULL;
    s_inited = true;
    s_status.inited = true;
    s_status.last_error = SERVICE_TOUCH_OK;
    return SERVICE_TOUCH_OK;
}

int service_touch_deinit(void)
{
    if (!s_inited) return SERVICE_TOUCH_ERR_NOT_INIT;
    memset(&s_status, 0, sizeof(s_status));
    s_touch = NULL;
    s_cb = NULL;
    s_cb_arg = NULL;
    s_pressed = false;
    s_inited = false;
    return SERVICE_TOUCH_OK;
}

int service_touch_start(void)
{
    if (!s_inited) return SERVICE_TOUCH_ERR_NOT_INIT;
    s_status.started = true;
    return SERVICE_TOUCH_OK;
}

int service_touch_stop(void)
{
    if (!s_inited) return SERVICE_TOUCH_ERR_NOT_INIT;
    s_status.started = false;
    s_pressed = false;
    return SERVICE_TOUCH_OK;
}

int service_touch_set_callback(service_touch_event_cb_t cb, void *arg)
{
    if (!s_inited) return SERVICE_TOUCH_ERR_NOT_INIT;
    s_cb = cb;
    s_cb_arg = arg;
    return SERVICE_TOUCH_OK;
}

int service_touch_get_last_point(service_touch_point_t *point)
{
    if (!s_inited) return SERVICE_TOUCH_ERR_NOT_INIT;
    if (!point) return SERVICE_TOUCH_ERR_PARAM;
    *point = s_status.last_point;
    return SERVICE_TOUCH_OK;
}

int service_touch_get_status(service_touch_status_t *status)
{
    if (!s_inited) return SERVICE_TOUCH_ERR_NOT_INIT;
    if (!status) return SERVICE_TOUCH_ERR_PARAM;
    *status = s_status;
    return SERVICE_TOUCH_OK;
}

int service_touch_poll_once(void)
{
    dal_touch_data_t data;
    service_touch_point_t point;
    int ret;

    if (!s_inited) return SERVICE_TOUCH_ERR_NOT_INIT;
    if (!s_status.started) return SERVICE_TOUCH_OK;

    memset(&data, 0, sizeof(data));
    ret = dal_touch_read(s_touch, &data);
    if (ret < 0) {
        s_status.last_error = ret;
        return SERVICE_TOUCH_ERR_DAL;
    }

    if (ret == 0 || data.num_points == 0) {
        if (s_pressed) {
            service_touch_point_t up = s_status.last_point;
            int dx;
            int dy;
            up.pressed = false;
            emit_event(SERVICE_TOUCH_EVENT_UP, up);
            dx = abs((int)up.x - (int)s_down_point.x);
            dy = abs((int)up.y - (int)s_down_point.y);
            emit_event((dx > s_cfg.swipe_threshold_px || dy > s_cfg.swipe_threshold_px) ?
                       SERVICE_TOUCH_EVENT_SWIPE : SERVICE_TOUCH_EVENT_CLICK, up);
        }
        s_pressed = false;
        return SERVICE_TOUCH_OK;
    }

    point = transform_point(data.points[0].x, data.points[0].y);
    if (!s_pressed) {
        s_pressed = true;
        s_down_point = point;
        emit_event(SERVICE_TOUCH_EVENT_DOWN, point);
    } else {
        emit_event(SERVICE_TOUCH_EVENT_NONE, point);
    }
    return SERVICE_TOUCH_OK;
}
