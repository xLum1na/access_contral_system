/**
 * @file    dal_camera.c
 * @brief   DAL 摄像头模块 — OV5647 MIPI CSI 实现
 *
 * 使用 ESP-IDF esp_video 初始化 MIPI CSI 视频设备，并通过 V4L2 接口
 * 完成格式设置、mmap buffer 管理和抓帧自检。
 */

#include "dal_camera.h"

#include "esp_cam_sensor_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "linux/videodev2.h"

#include "driver/i2c_master.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#define TAG "DAL_CAMERA"

#define DAL_CAMERA_MAX_BUFFERS 4
#define DAL_CAMERA_VIDEO_INIT_FLAGS (ESP_VIDEO_INIT_FLAGS_MIPI_CSI | ESP_VIDEO_INIT_FLAGS_ISP)

/* ================================================================
 *  内部数据结构
 * ================================================================ */

typedef struct {
    int      fd;
    bool     inited;
    bool     video_inited;
    bool     streaming;
    bool     buffers_requested;
    bool     buffers_queued;
    bool     active_frame_valid;
    uint32_t buffer_count;
    void    *buffers[DAL_CAMERA_MAX_BUFFERS];
    size_t   buffer_lengths[DAL_CAMERA_MAX_BUFFERS];
    struct v4l2_buffer active_buf;
    dal_camera_config_t cfg;
    dal_camera_info_t   info;
} dal_camera_internal_t;

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/**
 * @brief 将 DAL 像素格式转换为 V4L2 FourCC
 */
static uint32_t dal_camera_to_v4l2_format(dal_camera_pixel_format_t fmt)
{
    switch (fmt) {
    case DAL_CAMERA_PIXEL_FORMAT_RAW8:
    case DAL_CAMERA_PIXEL_FORMAT_DEFAULT:
        return V4L2_PIX_FMT_SBGGR8;
    case DAL_CAMERA_PIXEL_FORMAT_RGB565:
        return V4L2_PIX_FMT_RGB565;
    case DAL_CAMERA_PIXEL_FORMAT_JPEG:
        return V4L2_PIX_FMT_JPEG;
    case DAL_CAMERA_PIXEL_FORMAT_RAW10:
    default:
        return V4L2_PIX_FMT_SBGGR8;
    }
}

/**
 * @brief 释放 V4L2 mmap buffer
 */
static void dal_camera_unmap_buffers(dal_camera_internal_t *ctx)
{
    if (!ctx) return;

    for (uint32_t i = 0; i < ctx->buffer_count; i++) {
        if (ctx->buffers[i] != NULL && ctx->buffers[i] != MAP_FAILED) {
            munmap(ctx->buffers[i], ctx->buffer_lengths[i]);
            ctx->buffers[i] = NULL;
            ctx->buffer_lengths[i] = 0;
        }
    }

    if (ctx->buffers_requested && ctx->fd >= 0) {
        struct v4l2_requestbuffers req = {0};
        req.count = 0;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        (void)ioctl(ctx->fd, VIDIOC_REQBUFS, &req);
        ctx->buffers_requested = false;
        ctx->buffers_queued = false;
    }
}

/**
 * @brief 将全部 mmap buffer 重新入队
 */
static int dal_camera_queue_all_buffers(dal_camera_internal_t *ctx)
{
    if (!ctx || ctx->fd < 0) return -1;
    if (ctx->buffers_queued) return 0;

    for (uint32_t i = 0; i < ctx->buffer_count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF[%u] 失败: errno=%d", (unsigned)i, errno);
            return -1;
        }
    }

    ctx->buffers_queued = true;
    return 0;
}

/**
 * @brief 清理 camera 内部资源
 */
static void dal_camera_cleanup(dal_camera_internal_t *ctx)
{
    if (!ctx) return;

    if (ctx->streaming) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        (void)ioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
        ctx->streaming = false;
    }

    if (ctx->active_frame_valid && ctx->fd >= 0) {
        (void)ioctl(ctx->fd, VIDIOC_QBUF, &ctx->active_buf);
        ctx->active_frame_valid = false;
    }
    ctx->buffers_queued = false;

    dal_camera_unmap_buffers(ctx);

    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    if (ctx->video_inited) {
        (void)esp_video_deinit_with_flags(DAL_CAMERA_VIDEO_INIT_FLAGS);
        ctx->video_inited = false;
    }
}

/**
 * @brief 读取 sensor chip id
 */
static void dal_camera_read_chip_id(dal_camera_internal_t *ctx)
{
    esp_cam_sensor_id_t chip_id = {0};
    struct v4l2_ext_control control[1] = {0};
    struct v4l2_ext_controls controls = {0};

    controls.ctrl_class = V4L2_CTRL_CLASS_ESP_CAM_IOCTL;
    controls.count = 1;
    controls.controls = control;
    control[0].id = ESP_CAM_SENSOR_IOC_G_CHIP_ID;
    control[0].p_u8 = (uint8_t *)&chip_id;
    control[0].size = sizeof(chip_id);

    if (ioctl(ctx->fd, VIDIOC_G_EXT_CTRLS, &controls) == 0) {
        ctx->info.sensor_pid = chip_id.pid;
        ctx->info.sensor_ver = chip_id.ver;
        ESP_LOGI(TAG, "OV5647 chip id: 0x%04x, ver=0x%02x", chip_id.pid, chip_id.ver);
    } else {
        ESP_LOGW(TAG, "读取 sensor chip id 失败: errno=%d", errno);
    }
}

/**
 * @brief 配置 V4L2 DQBUF 超时
 */
static void dal_camera_set_dqbuf_timeout(dal_camera_internal_t *ctx, uint32_t timeout_ms)
{
    struct timeval timeout = {
        .tv_sec = (time_t)(timeout_ms / 1000U),
        .tv_usec = (suseconds_t)((timeout_ms % 1000U) * 1000U),
    };

    if (ioctl(ctx->fd, VIDIOC_S_DQBUF_TIMEOUT, &timeout) != 0) {
        ESP_LOGW(TAG, "设置 DQBUF 超时失败: errno=%d", errno);
    }
}

/* ================================================================
 *  Public API
 * ================================================================ */

int dal_camera_init(dal_camera_handle_t *handle, const dal_camera_config_t *cfg)
{
    if (!handle || !cfg) return -1;
    if (cfg->reuse_i2c_bus && cfg->i2c_bus_handle == NULL) return -1;

    dal_camera_internal_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return -1;

    ctx->fd = -1;
    memcpy(&ctx->cfg, cfg, sizeof(*cfg));
    ctx->cfg.device_path = cfg->device_path ? cfg->device_path : ESP_VIDEO_MIPI_CSI_DEVICE_NAME;
    ctx->cfg.width = (cfg->width > 0) ? cfg->width : 800;
    ctx->cfg.height = (cfg->height > 0) ? cfg->height : 800;
    ctx->cfg.buffer_count = (cfg->buffer_count > 0) ? cfg->buffer_count : 2;
    if (ctx->cfg.buffer_count > DAL_CAMERA_MAX_BUFFERS) {
        ctx->cfg.buffer_count = DAL_CAMERA_MAX_BUFFERS;
    }
    ctx->cfg.dqbuf_timeout_ms = (cfg->dqbuf_timeout_ms > 0) ? cfg->dqbuf_timeout_ms : 2000;
    ctx->cfg.self_test_max_tries = (cfg->self_test_max_tries > 0) ? cfg->self_test_max_tries : 3;

    esp_video_init_csi_config_t csi_cfg = {
        .sccb_config = {
            .init_sccb = !ctx->cfg.reuse_i2c_bus,
            .freq = ctx->cfg.sccb_freq_hz,
        },
        .reset_pin = ctx->cfg.reset_pin,
        .pwdn_pin = ctx->cfg.pwdn_pin,
    };
    if (ctx->cfg.reuse_i2c_bus) {
        csi_cfg.sccb_config.i2c_handle = (i2c_master_bus_handle_t)ctx->cfg.i2c_bus_handle;
    } else {
        csi_cfg.sccb_config.i2c_config.port = (uint8_t)ctx->cfg.sccb_i2c_port;
        csi_cfg.sccb_config.i2c_config.sda_pin = (gpio_num_t)ctx->cfg.sccb_sda_pin;
        csi_cfg.sccb_config.i2c_config.scl_pin = (gpio_num_t)ctx->cfg.sccb_scl_pin;
    }

    esp_video_init_config_t video_cfg = {
        .csi = &csi_cfg,
    };

    esp_err_t ret = esp_video_init_with_flags(&video_cfg, DAL_CAMERA_VIDEO_INIT_FLAGS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_video MIPI CSI 初始化失败: %d", ret);
        dal_camera_cleanup(ctx);
        free(ctx);
        return ret;
    }
    ctx->video_inited = true;
    ESP_LOGI(TAG, "esp_video MIPI CSI 初始化成功");

    ctx->fd = open(ctx->cfg.device_path, O_RDONLY);
    if (ctx->fd < 0) {
        ESP_LOGE(TAG, "打开视频设备失败: %s, errno=%d", ctx->cfg.device_path, errno);
        dal_camera_cleanup(ctx);
        free(ctx);
        return -1;
    }
    ESP_LOGI(TAG, "video device opened: %s", ctx->cfg.device_path);

    struct v4l2_capability cap = {0};
    if (ioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) != 0) {
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP 失败: errno=%d", errno);
        dal_camera_cleanup(ctx);
        free(ctx);
        return -1;
    }

    strncpy(ctx->info.driver, (const char *)cap.driver, sizeof(ctx->info.driver) - 1);
    strncpy(ctx->info.card, (const char *)cap.card, sizeof(ctx->info.card) - 1);
    strncpy(ctx->info.bus_info, (const char *)cap.bus_info, sizeof(ctx->info.bus_info) - 1);
    ESP_LOGI(TAG, "driver=%s card=%s bus=%s", ctx->info.driver, ctx->info.card, ctx->info.bus_info);

    dal_camera_read_chip_id(ctx);

    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .fmt.pix.width = ctx->cfg.width,
        .fmt.pix.height = ctx->cfg.height,
        .fmt.pix.pixelformat = dal_camera_to_v4l2_format(ctx->cfg.pixel_format),
    };
    if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) != 0) {
        ESP_LOGE(TAG, "VIDIOC_S_FMT 失败: %ux%u, errno=%d",
                 (unsigned)ctx->cfg.width, (unsigned)ctx->cfg.height, errno);
        dal_camera_cleanup(ctx);
        free(ctx);
        return -1;
    }

    ctx->info.width = fmt.fmt.pix.width;
    ctx->info.height = fmt.fmt.pix.height;
    ctx->info.pixelformat = fmt.fmt.pix.pixelformat;
    ESP_LOGI(TAG, "format set: %ux%u " V4L2_FMT_STR,
             (unsigned)ctx->info.width,
             (unsigned)ctx->info.height,
             V4L2_FMT_STR_ARG(ctx->info.pixelformat));

    dal_camera_set_dqbuf_timeout(ctx, ctx->cfg.dqbuf_timeout_ms);

    struct v4l2_requestbuffers req = {0};
    req.count = ctx->cfg.buffer_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) != 0 || req.count == 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS 失败: errno=%d", errno);
        dal_camera_cleanup(ctx);
        free(ctx);
        return -1;
    }
    ctx->buffers_requested = true;
    ctx->buffer_count = req.count;
    if (ctx->buffer_count > DAL_CAMERA_MAX_BUFFERS) {
        ctx->buffer_count = DAL_CAMERA_MAX_BUFFERS;
    }

    for (uint32_t i = 0; i < ctx->buffer_count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF[%u] 失败: errno=%d", (unsigned)i, errno);
            dal_camera_cleanup(ctx);
            free(ctx);
            return -1;
        }

        ctx->buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                               MAP_SHARED, ctx->fd, buf.m.offset);
        if (ctx->buffers[i] == MAP_FAILED) {
            ESP_LOGE(TAG, "mmap buffer[%u] 失败: errno=%d", (unsigned)i, errno);
            ctx->buffers[i] = NULL;
            dal_camera_cleanup(ctx);
            free(ctx);
            return -1;
        }
        ctx->buffer_lengths[i] = buf.length;

    }

    ctx->inited = true;
    *handle = (dal_camera_handle_t)ctx;
    ESP_LOGI(TAG, "初始化完成，buffer=%u", (unsigned)ctx->buffer_count);
    return 0;
}

int dal_camera_deinit(dal_camera_handle_t handle)
{
    dal_camera_internal_t *ctx = (dal_camera_internal_t *)handle;
    if (!ctx || !ctx->inited) return -1;

    ctx->inited = false;
    dal_camera_cleanup(ctx);
    free(ctx);
    ESP_LOGI(TAG, "已释放");
    return 0;
}

int dal_camera_get_info(dal_camera_handle_t handle, dal_camera_info_t *info)
{
    dal_camera_internal_t *ctx = (dal_camera_internal_t *)handle;
    if (!ctx || !ctx->inited || !info) return -1;

    memcpy(info, &ctx->info, sizeof(*info));
    return 0;
}

int dal_camera_start(dal_camera_handle_t handle)
{
    dal_camera_internal_t *ctx = (dal_camera_internal_t *)handle;
    if (!ctx || !ctx->inited) return -1;
    if (ctx->streaming) return 0;

    if (dal_camera_queue_all_buffers(ctx) != 0) {
        return -1;
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON 失败: errno=%d", errno);
        return -1;
    }

    ctx->streaming = true;
    return 0;
}

int dal_camera_stop(dal_camera_handle_t handle)
{
    dal_camera_internal_t *ctx = (dal_camera_internal_t *)handle;
    if (!ctx || !ctx->inited) return -1;
    if (!ctx->streaming) return 0;

    if (ctx->active_frame_valid) {
        (void)ioctl(ctx->fd, VIDIOC_QBUF, &ctx->active_buf);
        ctx->active_frame_valid = false;
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_STREAMOFF, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMOFF 失败: errno=%d", errno);
        return -1;
    }

    ctx->streaming = false;
    ctx->buffers_queued = false;
    return 0;
}

int dal_camera_capture_frame(dal_camera_handle_t handle,
                             dal_camera_frame_t *frame,
                             uint32_t timeout_ms)
{
    dal_camera_internal_t *ctx = (dal_camera_internal_t *)handle;
    if (!ctx || !ctx->inited || !frame) return -1;
    if (!ctx->streaming || ctx->active_frame_valid) return -1;

    if (timeout_ms > 0) {
        dal_camera_set_dqbuf_timeout(ctx, timeout_ms);
    }

    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(ctx->fd, VIDIOC_DQBUF, &buf) != 0) {
        ESP_LOGE(TAG, "VIDIOC_DQBUF 失败: errno=%d", errno);
        return -1;
    }

    if (buf.index >= ctx->buffer_count) {
        ESP_LOGE(TAG, "无效 buffer index=%u", (unsigned)buf.index);
        (void)ioctl(ctx->fd, VIDIOC_QBUF, &buf);
        return -1;
    }

    ctx->active_buf = buf;
    ctx->active_frame_valid = true;

    memset(frame, 0, sizeof(*frame));
    frame->data = ctx->buffers[buf.index];
    frame->size = buf.bytesused;
    frame->width = ctx->info.width;
    frame->height = ctx->info.height;
    frame->pixelformat = ctx->info.pixelformat;
    frame->sequence = buf.sequence;
    frame->timestamp_us = (uint64_t)buf.timestamp.tv_sec * 1000000ULL +
                          (uint64_t)buf.timestamp.tv_usec;
    frame->priv = (void *)(uintptr_t)(buf.index + 1U);

    if ((buf.flags & V4L2_BUF_FLAG_ERROR) || !(buf.flags & V4L2_BUF_FLAG_DONE) || buf.bytesused == 0) {
        ESP_LOGW(TAG, "无效帧: index=%u flags=0x%lx bytesused=%lu",
                 (unsigned)buf.index,
                 (unsigned long)buf.flags,
                 (unsigned long)buf.bytesused);
        (void)dal_camera_release_frame(handle, frame);
        return -1;
    }

    return 0;
}

int dal_camera_release_frame(dal_camera_handle_t handle,
                             const dal_camera_frame_t *frame)
{
    (void)frame;

    dal_camera_internal_t *ctx = (dal_camera_internal_t *)handle;
    if (!ctx || !ctx->inited || !ctx->active_frame_valid) return -1;

    if (ioctl(ctx->fd, VIDIOC_QBUF, &ctx->active_buf) != 0) {
        ESP_LOGE(TAG, "VIDIOC_QBUF release 失败: errno=%d", errno);
        return -1;
    }

    ctx->active_frame_valid = false;
    return 0;
}

int dal_camera_self_test(dal_camera_handle_t handle)
{
    dal_camera_internal_t *ctx = (dal_camera_internal_t *)handle;
    if (!ctx || !ctx->inited) return -1;

    int ret = dal_camera_start(handle);
    if (ret != 0) {
        return ret;
    }

    int result = -1;
    for (uint32_t i = 0; i < ctx->cfg.self_test_max_tries; i++) {
        dal_camera_frame_t frame = {0};
        ret = dal_camera_capture_frame(handle, &frame, ctx->cfg.dqbuf_timeout_ms);
        if (ret == 0) {
            ESP_LOGI(TAG,
                     "frame captured: index=%u bytesused=%lu sequence=%lu format=" V4L2_FMT_STR,
                     (unsigned)((uintptr_t)frame.priv ? ((uintptr_t)frame.priv - 1U) : 0U),
                     (unsigned long)frame.size,
                     (unsigned long)frame.sequence,
                     V4L2_FMT_STR_ARG(frame.pixelformat));
            (void)dal_camera_release_frame(handle, &frame);
            result = 0;
            break;
        }
        ESP_LOGW(TAG, "摄像头自检抓帧重试: %lu/%lu",
                 (unsigned long)(i + 1U),
                 (unsigned long)ctx->cfg.self_test_max_tries);
    }

    (void)dal_camera_stop(handle);
    if (result == 0) {
        ESP_LOGI(TAG, "自检通过");
    } else {
        ESP_LOGE(TAG, "自检失败");
    }
    return result;
}
