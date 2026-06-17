/**
 * @file    service_face_identify.h
 * @brief   人脸识别服务 — 特征提取与 1:N 比对接口
 *
 * 当前阶段先提供模型未就绪/占位模式和 service_db 对接骨架。
 * 真实特征模型接入后，在 C++ 实现中补充 ESP-DL 特征提取逻辑。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_FACE_IDENTIFY_H
#define SERVICE_FACE_IDENTIFY_H

#include <stdint.h>
#include <stdbool.h>
#include "dal_camera.h"
#include "service_face_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *service_face_identify_handle_t;

typedef enum {
    SERVICE_FACE_IDENTIFY_OK                  =  0,
    SERVICE_FACE_IDENTIFY_ERR_PARAM           = -1,
    SERVICE_FACE_IDENTIFY_ERR_NO_MEM          = -2,
    SERVICE_FACE_IDENTIFY_ERR_MODEL_NOT_FOUND = -3,
    SERVICE_FACE_IDENTIFY_ERR_MODEL_NOT_READY = -4,
    SERVICE_FACE_IDENTIFY_ERR_UNSUPPORTED_FMT = -5,
    SERVICE_FACE_IDENTIFY_ERR_EXTRACT         = -6,
    SERVICE_FACE_IDENTIFY_ERR_NOT_MATCH       = -7,
} service_face_identify_err_t;

typedef struct {
    uint32_t uid;          /**< 匹配到的用户 ID，0 表示未匹配 */
    float    similarity;   /**< 相似度 */
    uint8_t  score;        /**< 0~100 评分 */
    bool     matched;      /**< 是否通过阈值 */
} service_face_match_t;

typedef struct {
    const char *feature_model_path; /**< 特征模型路径 */
    uint32_t    model_id;           /**< 特征模型 ID */
    float       match_threshold;    /**< 匹配阈值 */
    bool        enable_placeholder; /**< 模型缺失时是否启用占位模式 */
} service_face_identify_config_t;

typedef struct {
    bool     model_ready;       /**< 真实模型是否已就绪 */
    bool     placeholder;       /**< 是否使用占位模式 */
    uint32_t model_id;          /**< 当前模型 ID */
    float    match_threshold;   /**< 匹配阈值 */
    char     model_path[128];   /**< 当前模型路径 */
} service_face_identify_status_t;

int service_face_identify_init(service_face_identify_handle_t *handle,
                               const service_face_identify_config_t *cfg);

int service_face_identify_extract(service_face_identify_handle_t handle,
                                  const dal_camera_frame_t *frame,
                                  const service_face_box_t *face,
                                  uint8_t *feature,
                                  uint32_t feature_buf_size,
                                  uint32_t *out_feature_size);

int service_face_identify_match(service_face_identify_handle_t handle,
                                const uint8_t *feature,
                                uint32_t feature_size,
                                service_face_match_t *out_match);

int service_face_identify_identify(service_face_identify_handle_t handle,
                                   const dal_camera_frame_t *frame,
                                   const service_face_box_t *face,
                                   service_face_match_t *out_match);

int service_face_identify_get_status(service_face_identify_handle_t handle,
                                     service_face_identify_status_t *status);

int service_face_identify_deinit(service_face_identify_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_FACE_IDENTIFY_H */
