/**
 * @file    service_face_detect.cpp
 * @brief   人脸检测服务 — ESP-DL 检测模型适配层骨架
 *
 * 当前阶段先搭建服务接口、模型路径检查和占位模式。真实 ESP-DL 模型
 * 到位后，在本文件内部补充模型加载、预处理、推理和后处理逻辑。
 */

#include "service_face_detect.h"

#include "dl_image_define.hpp"
#include "pal_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "FACE_DETECT"

/* ================================================================
 *  内部数据结构
 * ================================================================ */

typedef struct {
    bool     inited;
    bool     model_ready;
    bool     placeholder;
    uint32_t model_id;
    float    score_threshold;
    float    nms_threshold;
    char     model_path[128];
} service_face_detect_ctx_t;

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

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

extern "C" int service_face_detect_init(service_face_detect_handle_t *handle,
                                        const service_face_detect_config_t *cfg)
{
    if (!handle || !cfg) return SERVICE_FACE_DETECT_ERR_PARAM;

    service_face_detect_ctx_t *ctx = (service_face_detect_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return SERVICE_FACE_DETECT_ERR_NO_MEM;

    ctx->model_id = cfg->model_id;
    ctx->score_threshold = cfg->score_threshold;
    ctx->nms_threshold = cfg->nms_threshold;
    ctx->placeholder = cfg->enable_placeholder;

    if (cfg->model_path) {
        strncpy(ctx->model_path, cfg->model_path, sizeof(ctx->model_path) - 1);
    }

    if (file_exists(ctx->model_path)) {
        /* TODO: 使用 ESP-DL fbs_loader / dl::Model 加载 face_detect.espdl。 */
        ctx->model_ready = false;
        PAL_LOGW(TAG, "检测模型文件存在但真实加载尚未实现: %s", ctx->model_path);
    } else {
        ctx->model_ready = false;
        if (ctx->placeholder) {
            PAL_LOGW(TAG, "检测模型不存在，启用占位模式: %s", ctx->model_path);
        } else {
            PAL_LOGE(TAG, "检测模型不存在: %s", ctx->model_path);
            free(ctx);
            return SERVICE_FACE_DETECT_ERR_MODEL_NOT_FOUND;
        }
    }

    ctx->inited = true;
    *handle = (service_face_detect_handle_t)ctx;
    return SERVICE_FACE_DETECT_OK;
}

extern "C" int service_face_detect_run(service_face_detect_handle_t handle,
                                       const dal_camera_frame_t *frame,
                                       service_face_box_t *boxes,
                                       int max_boxes,
                                       int *out_count)
{
    service_face_detect_ctx_t *ctx = (service_face_detect_ctx_t *)handle;
    if (!ctx || !ctx->inited || !frame || !out_count) return SERVICE_FACE_DETECT_ERR_PARAM;

    *out_count = 0;

    if (!ctx->model_ready) {
        if (!ctx->placeholder) {
            return SERVICE_FACE_DETECT_ERR_MODEL_NOT_READY;
        }
        if (!boxes || max_boxes <= 0) {
            return SERVICE_FACE_DETECT_OK;
        }

        /* 占位检测：返回画面中心区域，仅用于打通业务链路，不能用于真实开门。 */
        service_face_box_t *box = &boxes[0];
        int w = (int)frame->width;
        int h = (int)frame->height;
        int side = (w < h ? w : h) / 3;
        int cx = w / 2;
        int cy = h / 2;
        box->x1 = cx - side / 2;
        box->y1 = cy - side / 2;
        box->x2 = cx + side / 2;
        box->y2 = cy + side / 2;
        box->score = 0.50f;
        box->landmark_count = 0;
        memset(box->landmarks, 0, sizeof(box->landmarks));
        *out_count = 1;
        return SERVICE_FACE_DETECT_OK;
    }

    /* TODO: 将 dal_camera_frame_t 封装为 dl::image::img_t 并执行真实模型推理。 */
    return SERVICE_FACE_DETECT_ERR_MODEL_NOT_READY;
}

extern "C" int service_face_detect_get_status(service_face_detect_handle_t handle,
                                              service_face_detect_status_t *status)
{
    service_face_detect_ctx_t *ctx = (service_face_detect_ctx_t *)handle;
    if (!ctx || !ctx->inited || !status) return SERVICE_FACE_DETECT_ERR_PARAM;

    memset(status, 0, sizeof(*status));
    status->model_ready = ctx->model_ready;
    status->placeholder = ctx->placeholder;
    status->model_id = ctx->model_id;
    strncpy(status->model_path, ctx->model_path, sizeof(status->model_path) - 1);
    return SERVICE_FACE_DETECT_OK;
}

extern "C" int service_face_detect_deinit(service_face_detect_handle_t handle)
{
    service_face_detect_ctx_t *ctx = (service_face_detect_ctx_t *)handle;
    if (!ctx || !ctx->inited) return SERVICE_FACE_DETECT_ERR_PARAM;

    ctx->inited = false;
    free(ctx);
    return SERVICE_FACE_DETECT_OK;
}
