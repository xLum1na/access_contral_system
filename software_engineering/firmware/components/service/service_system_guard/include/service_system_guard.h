/**
 * @file    service_system_guard.h
 * @brief   系统守护服务 — 任务心跳与故障状态管理
 *
 * 当前安全骨架只维护软件心跳和故障状态，不接硬件 WDT，
 * 不直接复位系统，也不直接控制继电器。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_SYSTEM_GUARD_H
#define SERVICE_SYSTEM_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SERVICE_GUARD_MAX_TASKS 8

typedef enum {
    SERVICE_GUARD_OK              =  0, /**< 成功 */
    SERVICE_GUARD_ERR_PARAM       = -1, /**< 参数错误 */
    SERVICE_GUARD_ERR_NOT_INIT    = -2, /**< 未初始化 */
    SERVICE_GUARD_ERR_FULL        = -3, /**< 任务表已满 */
    SERVICE_GUARD_ERR_NOT_FOUND   = -4, /**< 未找到任务 */
} service_guard_err_t;

typedef enum {
    SERVICE_GUARD_LEVEL_NONE = 0,       /**< 无故障 */
    SERVICE_GUARD_LEVEL_WARNING,        /**< 警告 */
    SERVICE_GUARD_LEVEL_ERROR,          /**< 错误 */
    SERVICE_GUARD_LEVEL_FATAL,          /**< 致命 */
} service_guard_level_t;

typedef enum {
    SERVICE_GUARD_FAULT_NONE = 0,       /**< 无故障 */
    SERVICE_GUARD_FAULT_CAMERA,         /**< 摄像头故障 */
    SERVICE_GUARD_FAULT_FACE,           /**< AI 服务故障 */
    SERVICE_GUARD_FAULT_UI,             /**< UI 故障 */
    SERVICE_GUARD_FAULT_MQTT,           /**< MQTT 故障 */
    SERVICE_GUARD_FAULT_DB,             /**< DB 故障 */
    SERVICE_GUARD_FAULT_SYSTEM,         /**< 系统故障 */
} service_guard_fault_t;

typedef struct {
    uint32_t check_period_ms;           /**< 检查周期，当前骨架仅保存 */
} service_system_guard_config_t;

typedef struct {
    uint32_t id;                        /**< 任务 ID */
    char     name[24];                  /**< 任务名 */
    uint32_t timeout_ms;                /**< 超时时间 */
    uint32_t feed_count;                /**< 喂狗次数 */
    bool     registered;                /**< 是否注册 */
} service_guard_task_status_t;

typedef struct {
    bool                  inited;       /**< 是否已初始化 */
    uint32_t              task_count;   /**< 已注册任务数 */
    uint32_t              fault_count;  /**< 故障计数 */
    service_guard_level_t highest_level;/**< 最高故障等级 */
    service_guard_fault_t last_fault;   /**< 最近故障 */
    int                   last_error;   /**< 最近错误码 */
    bool                  fatal_pending;/**< 是否存在待处理 FATAL */
} service_system_guard_status_t;

int service_system_guard_init(const service_system_guard_config_t *cfg);
int service_system_guard_deinit(void);
int service_system_guard_register_task(uint32_t id,
                                       const char *name,
                                       uint32_t timeout_ms);
int service_system_guard_feed(uint32_t id);
int service_system_guard_report_fault(service_guard_fault_t fault,
                                      service_guard_level_t level,
                                      int error_code,
                                      const char *detail);
int service_system_guard_get_status(service_system_guard_status_t *status);
int service_system_guard_get_task_status(uint32_t id,
                                         service_guard_task_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_SYSTEM_GUARD_H */
