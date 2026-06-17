/**
 * @file    service_camera.h
 * @brief   摄像头服务 — 摄像帧所有权中心
 *
 * 本服务封装 dal_camera 的 start/stop/capture/release。消费者只能通过
 * 队列接收只读帧描述符，不得保存 frame.data 超过 max_hold_ms，也不得
 * 调用 dal_camera_release_frame()。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_CAMERA_H
#define SERVICE_CAMERA_H

#include <stdbool.h>
#include <stdint.h>
#include "dal_camera.h"
#include "osal_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVICE_CAMERA_OK            =  0, /**< 成功 */
    SERVICE_CAMERA_ERR_PARAM     = -1, /**< 参数错误 */
    SERVICE_CAMERA_ERR_NOT_INIT  = -2, /**< 未初始化 */
    SERVICE_CAMERA_ERR_STATE     = -3, /**< 状态错误 */
    SERVICE_CAMERA_ERR_DAL       = -4, /**< DAL 错误 */
} service_camera_err_t;

typedef enum {
    SERVICE_CAMERA_CH_PREVIEW = 0,     /**< UI 预览通道 */
    SERVICE_CAMERA_CH_AI,              /**< AI 推理通道 */
    SERVICE_CAMERA_CH_DEBUG,           /**< 调试通道 */
    SERVICE_CAMERA_CH_MAX,
} service_camera_channel_t;

typedef struct {
    uint32_t capture_timeout_ms;        /**< 抓帧超时 */
    uint32_t max_hold_ms;               /**< 消费者最长持有时间 */
    uint32_t restart_after_timeout_count; /**< 连续超时重启阈值 */
} service_camera_config_t;

typedef struct {
    uint32_t           frame_id;        /**< 服务内帧 ID */
    dal_camera_frame_t frame;           /**< 只读帧描述符 */
} service_camera_frame_msg_t;

typedef struct {
    bool     inited;                    /**< 是否初始化 */
    bool     started;                   /**< 是否已 start */
    uint32_t captured_count;            /**< 成功抓帧数 */
    uint32_t dropped_preview_count;     /**< preview 丢帧数 */
    uint32_t dropped_ai_count;          /**< AI 丢帧数 */
    uint32_t dropped_debug_count;       /**< debug 丢帧数 */
    uint32_t timeout_count;             /**< 超时次数 */
    uint32_t restart_count;             /**< 重启 stream 次数 */
    int      last_error;                /**< 最近错误 */
} service_camera_status_t;

int service_camera_init(dal_camera_handle_t camera,
                        const service_camera_config_t *cfg);
int service_camera_deinit(void);
int service_camera_start(void);
int service_camera_stop(void);
int service_camera_subscribe(service_camera_channel_t channel, osal_queue_t queue);
int service_camera_unsubscribe(service_camera_channel_t channel);
int service_camera_get_status(service_camera_status_t *status);

/**
 * @brief 执行一次同步抓帧/分发/释放
 *
 * 该函数用于安全骨架和宿主机测试。后续若引入 camera task，可由 task
 * 周期调用本函数。
 *
 * @return 0 成功，负数错误码
 */
int service_camera_poll_once(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_CAMERA_H */
