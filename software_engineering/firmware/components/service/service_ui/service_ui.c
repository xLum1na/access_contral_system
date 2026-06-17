/**
 * @file    service_ui.c
 * @brief   UI 服务实现
 */

#include "service_ui.h"

#include "osal_queue.h"
#include "pal_log.h"

#include <string.h>

#define TAG "SERVICE_UI"
#define SERVICE_UI_QUEUE_DEPTH 8

static bool s_inited;
static dal_display_handle_t s_display;
static osal_queue_t s_queue;
static service_ui_status_t s_status;

int service_ui_init(dal_display_handle_t display)
{
    if (!display) return SERVICE_UI_ERR_PARAM;
    memset(&s_status, 0, sizeof(s_status));
    s_queue = osal_queue_create(sizeof(service_ui_event_t), SERVICE_UI_QUEUE_DEPTH);
    if (!s_queue) return SERVICE_UI_ERR_NO_MEM;
    s_display = display;
    s_inited = true;
    s_status.inited = true;
    s_status.current_page = SERVICE_UI_PAGE_STANDBY;
    s_status.system_state = SERVICE_UI_SYS_IDLE;
    return SERVICE_UI_OK;
}

int service_ui_deinit(void)
{
    if (!s_inited) return SERVICE_UI_ERR_NOT_INIT;
    if (s_status.started) {
        (void)service_ui_stop();
    }
    osal_queue_delete(s_queue);
    s_queue = NULL;
    s_display = NULL;
    memset(&s_status, 0, sizeof(s_status));
    s_inited = false;
    return SERVICE_UI_OK;
}

int service_ui_start(void)
{
    if (!s_inited) return SERVICE_UI_ERR_NOT_INIT;
    if (s_status.started) return SERVICE_UI_OK;
    (void)dal_display_on(s_display);
    (void)dal_display_set_backlight(s_display, 80);
    s_status.started = true;
    return SERVICE_UI_OK;
}

int service_ui_stop(void)
{
    if (!s_inited) return SERVICE_UI_ERR_NOT_INIT;
    if (!s_status.started) return SERVICE_UI_OK;
    (void)dal_display_set_backlight(s_display, 0);
    (void)dal_display_off(s_display);
    s_status.started = false;
    return SERVICE_UI_OK;
}

int service_ui_post_event(const service_ui_event_t *event)
{
    if (!s_inited) return SERVICE_UI_ERR_NOT_INIT;
    if (!event) return SERVICE_UI_ERR_PARAM;
    if (!osal_queue_send(s_queue, event, 0)) {
        s_status.drop_count++;
        return SERVICE_UI_ERR_QUEUE;
    }
    s_status.event_count++;
    return SERVICE_UI_OK;
}

int service_ui_show_page(service_ui_page_t page)
{
    if (!s_inited) return SERVICE_UI_ERR_NOT_INIT;
    if (page > SERVICE_UI_PAGE_FAULT) return SERVICE_UI_ERR_PARAM;
    s_status.current_page = page;
    if (s_status.started) {
        (void)dal_display_fill(s_display, 0, 0, 1, 1, (uint32_t)page);
    }
    return SERVICE_UI_OK;
}

int service_ui_show_identify_result(uint32_t uid,
                                    bool passed,
                                    uint8_t score,
                                    const char *name)
{
    if (!s_inited) return SERVICE_UI_ERR_NOT_INIT;
    s_status.current_page = SERVICE_UI_PAGE_RESULT;
    s_status.last_uid = uid;
    s_status.last_passed = passed;
    s_status.last_score = score;
    memset(s_status.last_name, 0, sizeof(s_status.last_name));
    if (name) {
        strncpy(s_status.last_name, name, sizeof(s_status.last_name) - 1);
    }
    if (s_status.started) {
        (void)dal_display_fill(s_display, 0, 0, 1, 1, passed ? 0x00FF00U : 0xFF0000U);
    }
    return SERVICE_UI_OK;
}

int service_ui_set_system_state(service_ui_system_state_t state)
{
    if (!s_inited) return SERVICE_UI_ERR_NOT_INIT;
    if (state > SERVICE_UI_SYS_ERROR) return SERVICE_UI_ERR_PARAM;
    s_status.system_state = state;
    if (state == SERVICE_UI_SYS_SLEEP && s_status.started) {
        (void)dal_display_set_backlight(s_display, 0);
    }
    return SERVICE_UI_OK;
}

int service_ui_get_status(service_ui_status_t *status)
{
    if (!s_inited) return SERVICE_UI_ERR_NOT_INIT;
    if (!status) return SERVICE_UI_ERR_PARAM;
    *status = s_status;
    return SERVICE_UI_OK;
}
