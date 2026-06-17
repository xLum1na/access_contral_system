/**
 * @file    service_event.h
 * @brief   Service 层统一事件定义
 *
 * 用于服务之间传递小型状态事件。payload 默认不使用；若使用，
 * 必须由具体 API 注释明确所有权和释放方。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_EVENT_H
#define SERVICE_EVENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVICE_EVT_NONE = 0,              /**< 空事件 */
    SERVICE_EVT_CAMERA_FRAME_READY,    /**< 摄像头帧就绪 */
    SERVICE_EVT_FACE_DETECTED,         /**< 检测到人脸 */
    SERVICE_EVT_FACE_IDENTIFIED,       /**< 人脸识别完成 */
    SERVICE_EVT_AUTH_PASS,             /**< 鉴权通过 */
    SERVICE_EVT_AUTH_DENY,             /**< 鉴权拒绝 */
    SERVICE_EVT_TOUCH,                 /**< 触摸事件 */
    SERVICE_EVT_UI_REQUEST,            /**< UI 请求 */
    SERVICE_EVT_LOG_READY,             /**< 日志事件 */
    SERVICE_EVT_MQTT_CONNECTED,        /**< MQTT 已连接 */
    SERVICE_EVT_MQTT_DISCONNECTED,     /**< MQTT 已断开 */
    SERVICE_EVT_FAULT,                 /**< 故障事件 */
} service_event_type_t;

typedef struct {
    service_event_type_t type;         /**< 事件类型 */
    uint32_t             timestamp;    /**< Unix 时间戳或系统 tick */
    uint32_t             source;       /**< 事件源 ID */
    uint32_t             arg0;         /**< 小型参数 0 */
    uint32_t             arg1;         /**< 小型参数 1 */
    void                *payload;      /**< 可选负载，所有权由具体 API 约定 */
} service_event_t;

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_EVENT_H */
