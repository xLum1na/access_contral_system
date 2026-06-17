/**
 * @file    service_camera.c
 * @brief   摄像头服务实现
 */

#include "service_camera.h"

#include "pal_log.h"
#include "service_system_guard.h"

#include <string.h>

#define TAG "SERVICE_CAMERA"

static bool s_inited;
static dal_camera_handle_t s_camera;
static service_camera_config_t s_cfg;
static osal_queue_t s_queues[SERVICE_CAMERA_CH_MAX];
static service_camera_status_t s_status;
static uint32_t s_frame_id;
static uint32_t s_consecutive_timeout;

static void camera_default_config(service_camera_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->capture_timeout_ms = 1000;
    cfg->max_hold_ms = 30;
    cfg->restart_after_timeout_count = 3;
}

static void camera_count_drop(service_camera_channel_t ch)
{
    if (ch == SERVICE_CAMERA_CH_PREVIEW) s_status.dropped_preview_count++;
    else if (ch == SERVICE_CAMERA_CH_AI) s_status.dropped_ai_count++;
    else if (ch == SERVICE_CAMERA_CH_DEBUG) s_status.dropped_debug_count++;
}

int service_camera_init(dal_camera_handle_t camera,
                        const service_camera_config_t *cfg)
{
    if (!camera) return SERVICE_CAMERA_ERR_PARAM;

    memset(&s_status, 0, sizeof(s_status));
    memset(s_queues, 0, sizeof(s_queues));
    camera_default_config(&s_cfg);
    if (cfg) {
        s_cfg = *cfg;
        if (s_cfg.capture_timeout_ms == 0) s_cfg.capture_timeout_ms = 1000;
        if (s_cfg.max_hold_ms == 0) s_cfg.max_hold_ms = 30;
        if (s_cfg.restart_after_timeout_count == 0) s_cfg.restart_after_timeout_count = 3;
    }
    s_camera = camera;
    s_frame_id = 0;
    s_consecutive_timeout = 0;
    s_inited = true;
    s_status.inited = true;
    s_status.last_error = SERVICE_CAMERA_OK;
    return SERVICE_CAMERA_OK;
}

int service_camera_deinit(void)
{
    if (!s_inited) return SERVICE_CAMERA_ERR_NOT_INIT;
    if (s_status.started) {
        (void)service_camera_stop();
    }
    memset(&s_status, 0, sizeof(s_status));
    memset(s_queues, 0, sizeof(s_queues));
    s_camera = NULL;
    s_inited = false;
    return SERVICE_CAMERA_OK;
}

int service_camera_start(void)
{
    int ret;

    if (!s_inited) return SERVICE_CAMERA_ERR_NOT_INIT;
    if (s_status.started) return SERVICE_CAMERA_OK;
    ret = dal_camera_start(s_camera);
    if (ret != 0) {
        s_status.last_error = ret;
        return SERVICE_CAMERA_ERR_DAL;
    }
    s_status.started = true;
    return SERVICE_CAMERA_OK;
}

int service_camera_stop(void)
{
    int ret;

    if (!s_inited) return SERVICE_CAMERA_ERR_NOT_INIT;
    if (!s_status.started) return SERVICE_CAMERA_OK;
    ret = dal_camera_stop(s_camera);
    if (ret != 0) {
        s_status.last_error = ret;
        return SERVICE_CAMERA_ERR_DAL;
    }
    s_status.started = false;
    return SERVICE_CAMERA_OK;
}

int service_camera_subscribe(service_camera_channel_t channel, osal_queue_t queue)
{
    if (!s_inited) return SERVICE_CAMERA_ERR_NOT_INIT;
    if (channel >= SERVICE_CAMERA_CH_MAX || !queue) return SERVICE_CAMERA_ERR_PARAM;
    s_queues[channel] = queue;
    return SERVICE_CAMERA_OK;
}

int service_camera_unsubscribe(service_camera_channel_t channel)
{
    if (!s_inited) return SERVICE_CAMERA_ERR_NOT_INIT;
    if (channel >= SERVICE_CAMERA_CH_MAX) return SERVICE_CAMERA_ERR_PARAM;
    s_queues[channel] = NULL;
    return SERVICE_CAMERA_OK;
}

int service_camera_get_status(service_camera_status_t *status)
{
    if (!s_inited) return SERVICE_CAMERA_ERR_NOT_INIT;
    if (!status) return SERVICE_CAMERA_ERR_PARAM;
    *status = s_status;
    return SERVICE_CAMERA_OK;
}

int service_camera_poll_once(void)
{
    dal_camera_frame_t frame;
    service_camera_frame_msg_t msg;
    int ret;

    if (!s_inited) return SERVICE_CAMERA_ERR_NOT_INIT;
    if (!s_status.started) return SERVICE_CAMERA_ERR_STATE;

    memset(&frame, 0, sizeof(frame));
    ret = dal_camera_capture_frame(s_camera, &frame, s_cfg.capture_timeout_ms);
    if (ret != 0) {
        s_status.timeout_count++;
        s_consecutive_timeout++;
        s_status.last_error = ret;
        if (s_consecutive_timeout >= s_cfg.restart_after_timeout_count) {
            (void)dal_camera_stop(s_camera);
            (void)dal_camera_start(s_camera);
            s_status.restart_count++;
            s_consecutive_timeout = 0;
            (void)service_system_guard_report_fault(SERVICE_GUARD_FAULT_CAMERA,
                                                    SERVICE_GUARD_LEVEL_WARNING,
                                                    ret, "camera capture timeout");
        }
        return SERVICE_CAMERA_ERR_DAL;
    }

    s_consecutive_timeout = 0;
    s_status.captured_count++;
    s_frame_id++;
    memset(&msg, 0, sizeof(msg));
    msg.frame_id = s_frame_id;
    msg.frame = frame;

    for (service_camera_channel_t ch = SERVICE_CAMERA_CH_PREVIEW; ch < SERVICE_CAMERA_CH_MAX; ch++) {
        if (s_queues[ch] && !osal_queue_send(s_queues[ch], &msg, 0)) {
            camera_count_drop(ch);
        }
    }

    (void)s_cfg.max_hold_ms;
    ret = dal_camera_release_frame(s_camera, &frame);
    if (ret != 0) {
        s_status.last_error = ret;
        return SERVICE_CAMERA_ERR_DAL;
    }
    s_status.last_error = SERVICE_CAMERA_OK;
    return SERVICE_CAMERA_OK;
}
