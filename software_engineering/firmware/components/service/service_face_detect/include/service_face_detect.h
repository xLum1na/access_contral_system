/**
 * @file    service_face_detect.h
 * @brief   人脸检测服务 — ESP-DL 检测模型适配层
 *
 * 对外提供 C API，内部可使用 C++/ESP-DL 实现模型加载与推理。
 * 当前阶段支持模型未就绪和占位检测模式，用于先打通业务状态机。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_FACE_DETECT_H
#define SERVICE_FACE_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include "dal_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  类型定义
 * ================================================================ */

typedef void *service_face_detect_handle_t;

typedef enum {
    SERVICE_FACE_DETECT_OK                 =  0,  /**< 成功 */
    SERVICE_FACE_DETECT_ERR_PARAM          = -1,  /**< 参数错误 */
    SERVICE_FACE_DETECT_ERR_NO_MEM         = -2,  /**< 内存不足 */
    SERVICE_FACE_DETECT_ERR_MODEL_NOT_FOUND= -3,  /**< 模型文件不存在 */
    SERVICE_FACE_DETECT_ERR_MODEL_NOT_READY= -4,  /**< 模型未就绪 */
    SERVICE_FACE_DETECT_ERR_UNSUPPORTED_FMT= -5,  /**< 图像格式不支持 */
    SERVICE_FACE_DETECT_ERR_INFER          = -6,  /**< 推理失败 */
} service_face_detect_err_t;

typedef struct {
    int   x1;              /**< 左上角 X */
    int   y1;              /**< 左上角 Y */
    int   x2;              /**< 右下角 X */
    int   y2;              /**< 右下角 Y */
    float score;           /**< 检测置信度 */
    int   landmark_count;  /**< 关键点数量 */
    int   landmarks[10];   /**< 5 点关键点：x0,y0...x4,y4 */
} service_face_box_t;

typedef struct {
    const char *model_path;       /**< 检测模型路径，如 /sdcard/model/face_detect.espdl */
    uint32_t    model_id;         /**< 检测模型 ID */
    float       score_threshold;  /**< 检测阈值 */
    float       nms_threshold;    /**< NMS 阈值 */
    bool        enable_placeholder; /**< 模型缺失时是否启用占位检测 */
} service_face_detect_config_t;

typedef struct {
    bool     model_ready;       /**< 真实模型是否已就绪 */
    bool     placeholder;       /**< 是否使用占位模式 */
    uint32_t model_id;          /**< 当前模型 ID */
    char     model_path[128];   /**< 当前模型路径 */
} service_face_detect_status_t;

/* ================================================================
 *  API
 * ================================================================ */

int service_face_detect_init(service_face_detect_handle_t *handle,
                             const service_face_detect_config_t *cfg);

int service_face_detect_run(service_face_detect_handle_t handle,
                            const dal_camera_frame_t *frame,
                            service_face_box_t *boxes,
                            int max_boxes,
                            int *out_count);

int service_face_detect_get_status(service_face_detect_handle_t handle,
                                   service_face_detect_status_t *status);

int service_face_detect_deinit(service_face_detect_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_FACE_DETECT_H */
