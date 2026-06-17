/**
 * @file    service_ui.h
 * @brief   UI 服务 — 最小页面状态与事件入口
 *
 * 当前安全骨架不接 LVGL，仅维护页面/状态/识别结果，并通过队列接收
 * UI 事件。其他服务不得直接操作显示控件。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_UI_H
#define SERVICE_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "dal_display.h"
#include "service_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVICE_UI_OK           =  0, /**< 成功 */
    SERVICE_UI_ERR_PARAM    = -1, /**< 参数错误 */
    SERVICE_UI_ERR_NOT_INIT = -2, /**< 未初始化 */
    SERVICE_UI_ERR_NO_MEM   = -3, /**< 内存不足 */
    SERVICE_UI_ERR_QUEUE    = -4, /**< 队列错误 */
} service_ui_err_t;

typedef enum {
    SERVICE_UI_PAGE_STANDBY = 0,  /**< 待机页 */
    SERVICE_UI_PAGE_IDENTIFYING,  /**< 识别中 */
    SERVICE_UI_PAGE_RESULT,       /**< 识别结果 */
    SERVICE_UI_PAGE_USER_MANAGE,  /**< 用户管理 */
    SERVICE_UI_PAGE_SETTINGS,     /**< 系统设置 */
    SERVICE_UI_PAGE_FAULT,        /**< 故障页 */
} service_ui_page_t;

typedef enum {
    SERVICE_UI_SYS_IDLE = 0,      /**< 空闲 */
    SERVICE_UI_SYS_WORKING,       /**< 工作中 */
    SERVICE_UI_SYS_SLEEP,         /**< 休眠 */
    SERVICE_UI_SYS_ERROR,         /**< 错误 */
} service_ui_system_state_t;

typedef struct {
    service_event_t event;        /**< 通用事件 */
    bool            debug;        /**< 是否调试/placeholder 结果 */
} service_ui_event_t;

typedef struct {
    bool                      inited;       /**< 是否初始化 */
    bool                      started;      /**< 是否启动 */
    service_ui_page_t         current_page; /**< 当前页面 */
    service_ui_system_state_t system_state; /**< 系统状态 */
    uint32_t                  last_uid;     /**< 最近识别 UID */
    bool                      last_passed;  /**< 最近识别是否通过 */
    uint8_t                   last_score;   /**< 最近识别分数 */
    char                      last_name[33];/**< 最近识别姓名 */
    uint32_t                  event_count;  /**< 事件数 */
    uint32_t                  drop_count;   /**< 丢事件数 */
} service_ui_status_t;

int service_ui_init(dal_display_handle_t display);
int service_ui_deinit(void);
int service_ui_start(void);
int service_ui_stop(void);
int service_ui_post_event(const service_ui_event_t *event);
int service_ui_show_page(service_ui_page_t page);
int service_ui_show_identify_result(uint32_t uid,
                                    bool passed,
                                    uint8_t score,
                                    const char *name);
int service_ui_set_system_state(service_ui_system_state_t state);
int service_ui_get_status(service_ui_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_UI_H */
