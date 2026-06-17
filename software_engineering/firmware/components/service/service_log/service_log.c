/**
 * @file    service_log.c
 * @brief   日志服务实现
 */

#include "service_log.h"

#include "pal_log.h"

#include <string.h>

#define TAG "SERVICE_LOG"
#define SERVICE_LOG_MSG_MAX 128

static service_log_status_t s_status;
static char s_last_message[SERVICE_LOG_MSG_MAX];

static int log_set_error(int err)
{
    s_status.last_error = err;
    if (err != SERVICE_LOG_OK) {
        s_status.fail_count++;
    }
    return err;
}

int service_log_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    memset(s_last_message, 0, sizeof(s_last_message));
    s_status.inited = true;
    s_status.last_error = SERVICE_LOG_OK;
    return SERVICE_LOG_OK;
}

int service_log_deinit(void)
{
    if (!s_status.inited) return SERVICE_LOG_ERR_NOT_INIT;
    memset(&s_status, 0, sizeof(s_status));
    memset(s_last_message, 0, sizeof(s_last_message));
    return SERVICE_LOG_OK;
}

int service_log_write(service_log_level_t level,
                      service_log_type_t type,
                      uint32_t uid,
                      const char *message)
{
    (void)uid;

    if (!s_status.inited) return SERVICE_LOG_ERR_NOT_INIT;
    if (!message || level > SERVICE_LOG_LEVEL_DEBUG || type > SERVICE_LOG_TYPE_ALARM) {
        return log_set_error(SERVICE_LOG_ERR_PARAM);
    }

    strncpy(s_last_message, message, sizeof(s_last_message) - 1);
    s_last_message[sizeof(s_last_message) - 1] = '\0';
    s_status.text_count++;
    s_status.last_error = SERVICE_LOG_OK;

    switch (level) {
    case SERVICE_LOG_LEVEL_ERROR: PAL_LOGE(TAG, "%s", s_last_message); break;
    case SERVICE_LOG_LEVEL_WARN:  PAL_LOGW(TAG, "%s", s_last_message); break;
    case SERVICE_LOG_LEVEL_INFO:  PAL_LOGI(TAG, "%s", s_last_message); break;
    case SERVICE_LOG_LEVEL_DEBUG: PAL_LOGD(TAG, "%s", s_last_message); break;
    default: break;
    }
    return SERVICE_LOG_OK;
}

int service_log_write_access(uint32_t uid,
                             db_log_event_t event_type,
                             uint8_t score,
                             float similarity,
                             uint32_t image_id)
{
    int ret;

    if (!s_status.inited) return SERVICE_LOG_ERR_NOT_INIT;
    ret = service_db_log_append(uid, (uint8_t)event_type, score, similarity, image_id);
    if (ret != 0) return log_set_error(SERVICE_LOG_ERR_DB);

    s_status.access_count++;
    s_status.last_error = SERVICE_LOG_OK;
    return SERVICE_LOG_OK;
}

int service_log_query(uint32_t start_time,
                      uint32_t end_time,
                      service_log_type_t type,
                      db_log_entry_t *list,
                      int max,
                      int *total)
{
    int ret;

    if (!s_status.inited) return SERVICE_LOG_ERR_NOT_INIT;
    if (type != SERVICE_LOG_TYPE_ACCESS) return SERVICE_LOG_ERR_UNSUPPORTED;
    if (!list || max <= 0) return log_set_error(SERVICE_LOG_ERR_PARAM);

    ret = service_db_log_query(start_time, end_time, 0, list, max, total);
    if (ret != 0) return log_set_error(SERVICE_LOG_ERR_DB);
    s_status.last_error = SERVICE_LOG_OK;
    return SERVICE_LOG_OK;
}

int service_log_export_csv(const char *path,
                           uint32_t start_time,
                           uint32_t end_time)
{
    (void)path;
    (void)start_time;
    (void)end_time;

    if (!s_status.inited) return SERVICE_LOG_ERR_NOT_INIT;
    return SERVICE_LOG_ERR_UNSUPPORTED;
}

int service_log_get_status(service_log_status_t *status)
{
    if (!s_status.inited) return SERVICE_LOG_ERR_NOT_INIT;
    if (!status) return SERVICE_LOG_ERR_PARAM;
    *status = s_status;
    return SERVICE_LOG_OK;
}
