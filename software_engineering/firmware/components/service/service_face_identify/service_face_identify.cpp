/**
 * @file    service_face_identify.cpp
 * @brief   人脸识别服务 — 特征提取与 1:N 比对骨架
 */

#include "service_face_identify.h"

#include "dl_image_define.hpp"
#include "pal_log.h"
#include "service_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "FACE_IDENTIFY"

/* ================================================================
 *  内部数据结构
 * ================================================================ */

typedef struct {
    bool     inited;
    bool     model_ready;
    bool     placeholder;
    uint32_t model_id;
    float    match_threshold;
    char     model_path[128];
} service_face_identify_ctx_t;

static bool file_exists(const char *path)
{
    if (!path || path[0] == '\0') return false;
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    fclose(fp);
    return true;
}

/* ================================================================
 *  Public API
 * ================================================================ */

extern "C" int service_face_identify_init(service_face_identify_handle_t *handle,
                                          const service_face_identify_config_t *cfg)
{
    if (!handle || !cfg) return SERVICE_FACE_IDENTIFY_ERR_PARAM;

    service_face_identify_ctx_t *ctx = (service_face_identify_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return SERVICE_FACE_IDENTIFY_ERR_NO_MEM;

    ctx->model_id = cfg->model_id;
    ctx->match_threshold = cfg->match_threshold;
    ctx->placeholder = cfg->enable_placeholder;
    if (cfg->feature_model_path) {
        strncpy(ctx->model_path, cfg->feature_model_path, sizeof(ctx->model_path) - 1);
    }

    if (file_exists(ctx->model_path)) {
        /* TODO: 使用 ESP-DL 加载 face_feature.espdl 并获取 feature_dim。 */
        ctx->model_ready = false;
        PAL_LOGW(TAG, "特征模型文件存在但真实加载尚未实现: %s", ctx->model_path);
    } else {
        ctx->model_ready = false;
        if (ctx->placeholder) {
            PAL_LOGW(TAG, "特征模型不存在，启用占位模式: %s", ctx->model_path);
        } else {
            PAL_LOGE(TAG, "特征模型不存在: %s", ctx->model_path);
            free(ctx);
            return SERVICE_FACE_IDENTIFY_ERR_MODEL_NOT_FOUND;
        }
    }

    int ret = service_db_face_load_all();
    if (ret != 0) {
        PAL_LOGW(TAG, "加载人脸特征库失败或为空: %d", ret);
    }

    ctx->inited = true;
    *handle = (service_face_identify_handle_t)ctx;
    return SERVICE_FACE_IDENTIFY_OK;
}

extern "C" int service_face_identify_extract(service_face_identify_handle_t handle,
                                             const dal_camera_frame_t *frame,
                                             const service_face_box_t *face,
                                             uint8_t *feature,
                                             uint32_t feature_buf_size,
                                             uint32_t *out_feature_size)
{
    service_face_identify_ctx_t *ctx = (service_face_identify_ctx_t *)handle;
    if (!ctx || !ctx->inited || !frame || !face || !out_feature_size) {
        return SERVICE_FACE_IDENTIFY_ERR_PARAM;
    }

    *out_feature_size = 0;

    if (!ctx->model_ready) {
        if (!ctx->placeholder) return SERVICE_FACE_IDENTIFY_ERR_MODEL_NOT_READY;

        /* 占位特征仅用于 DB/流程验证，不能用于真实识别。 */
        if (!feature || feature_buf_size < 16) return SERVICE_FACE_IDENTIFY_ERR_PARAM;
        for (uint32_t i = 0; i < 16; i++) {
            feature[i] = (uint8_t)(0xA0U + i);
        }
        *out_feature_size = 16;
        return SERVICE_FACE_IDENTIFY_OK;
    }

    /* TODO: 基于 face box/landmarks 做人脸对齐并调用 ESP-DL 特征模型。 */
    return SERVICE_FACE_IDENTIFY_ERR_MODEL_NOT_READY;
}

extern "C" int service_face_identify_match(service_face_identify_handle_t handle,
                                           const uint8_t *feature,
                                           uint32_t feature_size,
                                           service_face_match_t *out_match)
{
    service_face_identify_ctx_t *ctx = (service_face_identify_ctx_t *)handle;
    if (!ctx || !ctx->inited || !feature || feature_size == 0 || !out_match) {
        return SERVICE_FACE_IDENTIFY_ERR_PARAM;
    }

    memset(out_match, 0, sizeof(*out_match));

    if (!ctx->model_ready) {
        return SERVICE_FACE_IDENTIFY_ERR_MODEL_NOT_READY;
    }

    /* TODO: 遍历 service_db_face_iterate()，按模型输出格式计算 similarity。 */
    return SERVICE_FACE_IDENTIFY_ERR_NOT_MATCH;
}

extern "C" int service_face_identify_identify(service_face_identify_handle_t handle,
                                              const dal_camera_frame_t *frame,
                                              const service_face_box_t *face,
                                              service_face_match_t *out_match)
{
    if (!out_match) return SERVICE_FACE_IDENTIFY_ERR_PARAM;
    memset(out_match, 0, sizeof(*out_match));

    uint8_t feature[256] = {0};
    uint32_t feature_size = 0;
    int ret = service_face_identify_extract(handle, frame, face,
                                            feature, sizeof(feature), &feature_size);
    if (ret != SERVICE_FACE_IDENTIFY_OK) return ret;

    return service_face_identify_match(handle, feature, feature_size, out_match);
}

extern "C" int service_face_identify_get_status(service_face_identify_handle_t handle,
                                                service_face_identify_status_t *status)
{
    service_face_identify_ctx_t *ctx = (service_face_identify_ctx_t *)handle;
    if (!ctx || !ctx->inited || !status) return SERVICE_FACE_IDENTIFY_ERR_PARAM;

    memset(status, 0, sizeof(*status));
    status->model_ready = ctx->model_ready;
    status->placeholder = ctx->placeholder;
    status->model_id = ctx->model_id;
    status->match_threshold = ctx->match_threshold;
    strncpy(status->model_path, ctx->model_path, sizeof(status->model_path) - 1);
    return SERVICE_FACE_IDENTIFY_OK;
}

extern "C" int service_face_identify_deinit(service_face_identify_handle_t handle)
{
    service_face_identify_ctx_t *ctx = (service_face_identify_ctx_t *)handle;
    if (!ctx || !ctx->inited) return SERVICE_FACE_IDENTIFY_ERR_PARAM;

    ctx->inited = false;
    free(ctx);
    return SERVICE_FACE_IDENTIFY_OK;
}
