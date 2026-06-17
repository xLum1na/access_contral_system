/**
 * @file    dal_camera.h
 * @brief   DAL 摄像头模块 — OV5647 MIPI CSI 抽象层
 *
 * 封装 ESP-IDF esp_video + V4L2 采集接口，向上提供摄像头初始化、
 * 启停流和抓帧 API。当前目标硬件为 JC-ESP32P4-M3-DEV + OV5647。
 *
 * 参考文档：ESP-IDF esp_video capture_stream 示例
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef DAL_CAMERA_H
#define DAL_CAMERA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  类型定义
 * ================================================================ */

typedef void *dal_camera_handle_t;

/** @brief 摄像头像素格式 */
typedef enum {
    DAL_CAMERA_PIXEL_FORMAT_DEFAULT = 0,   /**< 使用当前 SDK 默认格式 */
    DAL_CAMERA_PIXEL_FORMAT_RAW8,          /**< 8-bit Bayer RAW */
    DAL_CAMERA_PIXEL_FORMAT_RAW10,         /**< 10-bit Bayer RAW */
    DAL_CAMERA_PIXEL_FORMAT_RGB565,        /**< RGB565 */
    DAL_CAMERA_PIXEL_FORMAT_JPEG,          /**< JPEG */
} dal_camera_pixel_format_t;

/** @brief 摄像头初始化配置 */
typedef struct {
    void    *i2c_bus_handle;       /**< 底层 i2c_master_bus_handle_t，可由 pal_i2c_get_bus_handle() 获取 */
    bool     reuse_i2c_bus;        /**< 是否复用已初始化 I2C 总线 */
    int      sccb_i2c_port;        /**< SCCB I2C 端口号 */
    int      sccb_sda_pin;         /**< SCCB SDA 引脚 */
    int      sccb_scl_pin;         /**< SCCB SCL 引脚 */
    uint32_t sccb_freq_hz;         /**< SCCB 频率 */
    int      reset_pin;            /**< Sensor reset 引脚，-1 表示不使用 */
    int      pwdn_pin;             /**< Sensor power-down 引脚，-1 表示不使用 */
    const char *device_path;       /**< V4L2 设备路径，默认 /dev/video0 */
    uint32_t width;                /**< 采集宽度 */
    uint32_t height;               /**< 采集高度 */
    dal_camera_pixel_format_t pixel_format; /**< 像素格式 */
    uint32_t buffer_count;         /**< V4L2 mmap buffer 数量 */
    uint32_t dqbuf_timeout_ms;     /**< DQBUF 超时，单位 ms */
    uint32_t self_test_max_tries;  /**< 自检抓帧最大尝试次数 */
} dal_camera_config_t;

/** @brief 摄像头信息 */
typedef struct {
    uint16_t sensor_pid;           /**< Sensor PID */
    uint16_t sensor_ver;           /**< Sensor version */
    uint32_t width;                /**< 当前宽度 */
    uint32_t height;               /**< 当前高度 */
    uint32_t pixelformat;          /**< V4L2 FourCC */
    char     driver[32];           /**< V4L2 driver 名称 */
    char     card[32];             /**< V4L2 card 名称 */
    char     bus_info[32];         /**< V4L2 bus 信息 */
} dal_camera_info_t;

/** @brief 摄像头帧 */
typedef struct {
    const void *data;              /**< 帧数据指针，release 前有效 */
    size_t      size;              /**< 有效数据长度 */
    uint32_t    width;             /**< 帧宽 */
    uint32_t    height;            /**< 帧高 */
    uint32_t    pixelformat;       /**< V4L2 FourCC */
    uint32_t    sequence;          /**< V4L2 帧序号 */
    uint64_t    timestamp_us;      /**< 时间戳，单位 us */
    void       *priv;              /**< DAL 内部私有数据 */
} dal_camera_frame_t;

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 初始化摄像头
 *
 * @param[out] handle 输出摄像头句柄
 * @param[in]  cfg    摄像头配置
 * @return 0 成功，负数失败
 */
int dal_camera_init(dal_camera_handle_t *handle, const dal_camera_config_t *cfg);

/**
 * @brief 反初始化摄像头
 *
 * @param handle 摄像头句柄
 * @return 0 成功，负数失败
 */
int dal_camera_deinit(dal_camera_handle_t handle);

/**
 * @brief 获取摄像头信息
 *
 * @param handle 摄像头句柄
 * @param[out] info 摄像头信息
 * @return 0 成功，负数失败
 */
int dal_camera_get_info(dal_camera_handle_t handle, dal_camera_info_t *info);

/**
 * @brief 开始视频流
 *
 * @param handle 摄像头句柄
 * @return 0 成功，负数失败
 */
int dal_camera_start(dal_camera_handle_t handle);

/**
 * @brief 停止视频流
 *
 * @param handle 摄像头句柄
 * @return 0 成功，负数失败
 */
int dal_camera_stop(dal_camera_handle_t handle);

/**
 * @brief 抓取一帧
 *
 * 成功后必须调用 dal_camera_release_frame() 归还 buffer。
 *
 * @param handle 摄像头句柄
 * @param[out] frame 输出帧信息
 * @param timeout_ms 超时，单位 ms；0 使用初始化配置默认值
 * @return 0 成功，负数失败
 */
int dal_camera_capture_frame(dal_camera_handle_t handle,
                             dal_camera_frame_t *frame,
                             uint32_t timeout_ms);

/**
 * @brief 释放抓取到的帧
 *
 * @param handle 摄像头句柄
 * @param frame  待释放帧
 * @return 0 成功，负数失败
 */
int dal_camera_release_frame(dal_camera_handle_t handle,
                             const dal_camera_frame_t *frame);

/**
 * @brief 摄像头自检，启动流并抓取一帧
 *
 * @param handle 摄像头句柄
 * @return 0 成功，负数失败
 */
int dal_camera_self_test(dal_camera_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* DAL_CAMERA_H */
