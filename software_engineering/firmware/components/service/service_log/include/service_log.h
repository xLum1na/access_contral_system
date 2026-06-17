/**
 * @file    service_log.h
 * @brief   日志服务 — 统一日志入口
 *
 * 在 service_db 通行日志之上提供统一入口。当前安全骨架仅同步写入
 * access log，普通文本日志保存在 RAM 统计中，CSV 导出暂未决策。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_LOG_H
#define SERVICE_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include "service_db.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVICE_LOG_OK              =  0,  /**< 成功 */
    SERVICE_LOG_ERR_PARAM       = -1,  /**< 参数错误 */
    SERVICE_LOG_ERR_NOT_INIT    = -2,  /**< 未初始化 */
    SERVICE_LOG_ERR_DB          = -3,  /**< DB 写入/查询失败 */
    SERVICE_LOG_ERR_UNSUPPORTED = -4,  /**< 功能暂不支持 */
} service_log_err_t;

typedef enum {
    SERVICE_LOG_LEVEL_ERROR = 0,       /**< 错误 */
    SERVICE_LOG_LEVEL_WARN,            /**< 警告 */
    SERVICE_LOG_LEVEL_INFO,            /**< 信息 */
    SERVICE_LOG_LEVEL_DEBUG,           /**< 调试 */
} service_log_level_t;

typedef enum {
    SERVICE_LOG_TYPE_ACCESS = 0,       /**< 通行日志 */
    SERVICE_LOG_TYPE_SYSTEM,           /**< 系统日志 */
    SERVICE_LOG_TYPE_ADMIN,            /**< 管理员操作 */
    SERVICE_LOG_TYPE_ALARM,            /**< 告警日志 */
} service_log_type_t;

typedef struct {
    bool     inited;                   /**< 是否已初始化 */
    uint32_t text_count;               /**< 普通文本日志计数 */
    uint32_t access_count;             /**< access 日志写入次数 */
    uint32_t fail_count;               /**< 失败计数 */
    int      last_error;               /**< 最近错误码 */
} service_log_status_t;

int service_log_init(void);
int service_log_deinit(void);
int service_log_write(service_log_level_t level,
                      service_log_type_t type,
                      uint32_t uid,
                      const char *message);
int service_log_write_access(uint32_t uid,
                             db_log_event_t event_type,
                             uint8_t score,
                             float similarity,
                             uint32_t image_id);
int service_log_query(uint32_t start_time,
                      uint32_t end_time,
                      service_log_type_t type,
                      db_log_entry_t *list,
                      int max,
                      int *total);
int service_log_export_csv(const char *path,
                           uint32_t start_time,
                           uint32_t end_time);
int service_log_get_status(service_log_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_LOG_H */
