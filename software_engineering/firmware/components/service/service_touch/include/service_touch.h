/**
 * @file    service_touch.h
 * @brief   触摸服务 — 单指触摸事件抽象
 *
 * 当前安全骨架仅处理第一触点，完成坐标变换、裁剪和基础点击/长按/滑动
 * 事件生成，不直接操作 UI 页面或执行管理员动作。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_TOUCH_H
#define SERVICE_TOUCH_H

#include <stdbool.h>
#include <stdint.h>
#include "dal_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVICE_TOUCH_OK           =  0, /**< 成功 */
    SERVICE_TOUCH_ERR_PARAM    = -1, /**< 参数错误 */
    SERVICE_TOUCH_ERR_NOT_INIT = -2, /**< 未初始化 */
    SERVICE_TOUCH_ERR_DAL      = -3, /**< DAL 错误 */
} service_touch_err_t;

typedef enum {
    SERVICE_TOUCH_EVENT_NONE = 0,    /**< 无事件 */
    SERVICE_TOUCH_EVENT_DOWN,        /**< 按下 */
    SERVICE_TOUCH_EVENT_UP,          /**< 抬起 */
    SERVICE_TOUCH_EVENT_CLICK,       /**< 点击 */
    SERVICE_TOUCH_EVENT_LONG_PRESS,  /**< 长按 */
    SERVICE_TOUCH_EVENT_SWIPE,       /**< 滑动 */
} service_touch_event_type_t;

typedef enum {
    SERVICE_TOUCH_ROTATE_0 = 0,      /**< 不旋转 */
    SERVICE_TOUCH_ROTATE_90,         /**< 旋转 90 度 */
    SERVICE_TOUCH_ROTATE_180,        /**< 旋转 180 度 */
    SERVICE_TOUCH_ROTATE_270,        /**< 旋转 270 度 */
} service_touch_rotate_t;

typedef struct {
    uint16_t x;                      /**< X 坐标 */
    uint16_t y;                      /**< Y 坐标 */
    bool     pressed;                /**< 是否按下 */
} service_touch_point_t;

typedef struct {
    service_touch_event_type_t type; /**< 事件类型 */
    service_touch_point_t      point;/**< 当前点 */
} service_touch_event_t;

typedef void (*service_touch_event_cb_t)(const service_touch_event_t *event, void *arg);

typedef struct {
    uint16_t               h_res;              /**< 屏幕宽 */
    uint16_t               v_res;              /**< 屏幕高 */
    service_touch_rotate_t rotate;             /**< 坐标旋转 */
    bool                   mirror_x;           /**< X 镜像 */
    bool                   mirror_y;           /**< Y 镜像 */
    uint32_t               long_press_ms;      /**< 长按阈值 */
    uint16_t               swipe_threshold_px; /**< 滑动阈值 */
} service_touch_config_t;

typedef struct {
    bool                  inited;              /**< 是否初始化 */
    bool                  started;             /**< 是否启动 */
    service_touch_point_t last_point;          /**< 最近点 */
    uint32_t              event_count;         /**< 事件数 */
    int                   last_error;          /**< 最近错误 */
} service_touch_status_t;

int service_touch_init(dal_touch_handle_t touch,
                       const service_touch_config_t *cfg);
int service_touch_deinit(void);
int service_touch_start(void);
int service_touch_stop(void);
int service_touch_set_callback(service_touch_event_cb_t cb, void *arg);
int service_touch_get_last_point(service_touch_point_t *point);
int service_touch_get_status(service_touch_status_t *status);
int service_touch_poll_once(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_TOUCH_H */
