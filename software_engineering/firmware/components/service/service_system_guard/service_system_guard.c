/**
 * @file    service_system_guard.c
 * @brief   系统守护服务实现
 */

#include "service_system_guard.h"

#include "pal_log.h"
#include "service_log.h"

#include <string.h>

#define TAG "SYS_GUARD"

static bool s_inited;
static service_guard_task_status_t s_tasks[SERVICE_GUARD_MAX_TASKS];
static service_system_guard_status_t s_status;
static uint32_t s_check_period_ms;

static int find_task(uint32_t id)
{
    for (int i = 0; i < SERVICE_GUARD_MAX_TASKS; i++) {
        if (s_tasks[i].registered && s_tasks[i].id == id) return i;
    }
    return -1;
}

int service_system_guard_init(const service_system_guard_config_t *cfg)
{
    memset(s_tasks, 0, sizeof(s_tasks));
    memset(&s_status, 0, sizeof(s_status));
    s_check_period_ms = cfg ? cfg->check_period_ms : 1000U;
    if (s_check_period_ms == 0) s_check_period_ms = 1000U;
    s_inited = true;
    s_status.inited = true;
    s_status.highest_level = SERVICE_GUARD_LEVEL_NONE;
    s_status.last_error = SERVICE_GUARD_OK;
    return SERVICE_GUARD_OK;
}

int service_system_guard_deinit(void)
{
    if (!s_inited) return SERVICE_GUARD_ERR_NOT_INIT;
    memset(s_tasks, 0, sizeof(s_tasks));
    memset(&s_status, 0, sizeof(s_status));
    s_check_period_ms = 0;
    s_inited = false;
    return SERVICE_GUARD_OK;
}

int service_system_guard_register_task(uint32_t id,
                                       const char *name,
                                       uint32_t timeout_ms)
{
    int index;

    if (!s_inited) return SERVICE_GUARD_ERR_NOT_INIT;
    if (id == 0 || !name || name[0] == '\0' || timeout_ms == 0) return SERVICE_GUARD_ERR_PARAM;

    index = find_task(id);
    if (index < 0) {
        for (int i = 0; i < SERVICE_GUARD_MAX_TASKS; i++) {
            if (!s_tasks[i].registered) {
                index = i;
                s_status.task_count++;
                break;
            }
        }
    }
    if (index < 0) return SERVICE_GUARD_ERR_FULL;

    memset(&s_tasks[index], 0, sizeof(s_tasks[index]));
    s_tasks[index].id = id;
    strncpy(s_tasks[index].name, name, sizeof(s_tasks[index].name) - 1);
    s_tasks[index].timeout_ms = timeout_ms;
    s_tasks[index].registered = true;
    return SERVICE_GUARD_OK;
}

int service_system_guard_feed(uint32_t id)
{
    int index;

    if (!s_inited) return SERVICE_GUARD_ERR_NOT_INIT;
    index = find_task(id);
    if (index < 0) return SERVICE_GUARD_ERR_NOT_FOUND;
    s_tasks[index].feed_count++;
    return SERVICE_GUARD_OK;
}

int service_system_guard_report_fault(service_guard_fault_t fault,
                                      service_guard_level_t level,
                                      int error_code,
                                      const char *detail)
{
    if (!s_inited) return SERVICE_GUARD_ERR_NOT_INIT;
    if (fault == SERVICE_GUARD_FAULT_NONE || level == SERVICE_GUARD_LEVEL_NONE ||
        level > SERVICE_GUARD_LEVEL_FATAL) {
        return SERVICE_GUARD_ERR_PARAM;
    }

    s_status.fault_count++;
    s_status.last_fault = fault;
    s_status.last_error = error_code;
    if (level > s_status.highest_level) {
        s_status.highest_level = level;
    }
    if (level == SERVICE_GUARD_LEVEL_FATAL) {
        s_status.fatal_pending = true;
    }

    PAL_LOGW(TAG, "fault=%d level=%d err=%d %s", fault, level, error_code,
             detail ? detail : "");
    (void)service_log_write(SERVICE_LOG_LEVEL_WARN, SERVICE_LOG_TYPE_SYSTEM, 0,
                            detail ? detail : "system fault");
    return SERVICE_GUARD_OK;
}

int service_system_guard_get_status(service_system_guard_status_t *status)
{
    if (!s_inited) return SERVICE_GUARD_ERR_NOT_INIT;
    if (!status) return SERVICE_GUARD_ERR_PARAM;
    *status = s_status;
    status->inited = true;
    (void)s_check_period_ms;
    return SERVICE_GUARD_OK;
}

int service_system_guard_get_task_status(uint32_t id,
                                         service_guard_task_status_t *status)
{
    int index;

    if (!s_inited) return SERVICE_GUARD_ERR_NOT_INIT;
    if (!status) return SERVICE_GUARD_ERR_PARAM;
    index = find_task(id);
    if (index < 0) return SERVICE_GUARD_ERR_NOT_FOUND;
    *status = s_tasks[index];
    return SERVICE_GUARD_OK;
}
