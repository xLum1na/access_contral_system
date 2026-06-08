/**
 * @file    dal_camera.h
 * @brief   DAL 摄像头模块 — 统一摄像头抽象层
 *
 * 封装 MIPI CSI 摄像头子系统，协调以下硬件：
 *   - IMX219 传感器（I2C 0x10，16-bit 寄存器地址）
 *   - ESP32-P4 MIPI CSI 主机（pal_mipi_csi）
 *
 * 向上层（Service）提供统一的摄像头 API，屏蔽底层硬件细节。
 *
 * 典型使用流程：
 *   1. dal_camera_init()     — 初始化传感器 + CSI 控制器
 *   2. dal_camera_capture()  — 阻塞获取一帧
 *   3. …重复 capture…
 *   4. dal_camera_deinit()   — 停止流 + 释放资源
 *
 * 参考：IMX219 数据手册 + ESP32-P4 TRM MIPI CSI 章节
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef DAL_CAMERA_H
#define DAL_CAMERA_H

#include <stdint.h>
#include <stdbool.h>
#include "imx219.h"
#include "pal_mipi_csi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 摄像头不透明句柄 */
typedef void *dal_camera_handle_t;

/* ================================================================
 *  配置结构体
 * ================================================================ */

/**
 * @brief 摄像头子系统完整配置
 */
typedef struct {
    /* ---- I2C 总线 ---- */
    void     *i2c_bus_handle;            /**< PAL I2C 总线句柄（已初始化） */

    /* ---- IMX219 传感器 ---- */
    uint16_t  imx219_i2c_addr;           /**< IMX219 I2C 地址（默认 0x10） */
    imx219_resolution_t resolution;       /**< 分辨率预设 */
    int       fps;                        /**< 目标帧率 */
    bool      flip_horizontal;            /**< 水平翻转 */
    bool      flip_vertical;              /**< 垂直翻转 */
    int       xclk_pin;                   /**< XVCLK GPIO，-1=板载晶振 */

    /* ---- MIPI CSI 主机 ---- */
    int       csi_ctlr_id;                /**< CSI 控制器编号（0 或 1） */
    uint8_t   csi_data_lane_num;          /**< CSI 数据通道数（1 或 2） */
    int       csi_lane_bit_rate_mbps;     /**< CSI 通道速率 (Mbps) */
    pal_mipi_csi_color_t  input_color;    /**< 输入颜色格式（传感器输出） */
    pal_mipi_csi_color_t  output_color;   /**< 期望输出颜色格式 */
    int       csi_queue_items;            /**< CSI 帧缓冲队列项数 */

    /* ---- 帧缓冲 ---- */
    bool      allocate_fb;                /**< 是否由 DAL 内部分配帧缓冲 */
} dal_camera_config_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

/**
 * @brief 初始化摄像头（传感器 init → CSI 控制器 init）
 *
 * @param[out] handle 返回的摄像头句柄
 * @param[in]  cfg    配置
 * @return 0 成功，负数失败
 */
int dal_camera_init(dal_camera_handle_t *handle,
                    const dal_camera_config_t *cfg);

/**
 * @brief 反初始化（停止流 → 释放传感器 → 释放 CSI）
 *
 * @param handle 摄像头句柄
 * @return 0 成功
 */
int dal_camera_deinit(dal_camera_handle_t handle);

/* ================================================================
 *  帧捕获 API
 * ================================================================ */

/**
 * @brief 获取一帧图像（阻塞）
 *
 * 内部调用 pal_mipi_csi_capture()，帧数据写入 DAL 管理的帧缓冲。
 *
 * @param handle      摄像头句柄
 * @param[out] frame  帧数据
 * @param timeout_ms  超时（ms），-1 = 永久等待
 * @return 0 成功，负数超时或错误
 */
int dal_camera_capture(dal_camera_handle_t handle,
                       pal_mipi_csi_frame_t *frame,
                       int timeout_ms);

/* ================================================================
 *  传感器控制 API
 * ================================================================ */

/**
 * @brief 设置曝光时间
 *
 * @param handle         摄像头句柄
 * @param exposure_lines 曝光行数
 * @return 0 成功
 */
int dal_camera_set_exposure(dal_camera_handle_t handle,
                            uint32_t exposure_lines);

/**
 * @brief 设置模拟增益
 *
 * @param handle 摄像头句柄
 * @param gain   增益值
 * @return 0 成功
 */
int dal_camera_set_gain(dal_camera_handle_t handle, uint16_t gain);

/**
 * @brief 获取当前分辨率宽高
 *
 * @param handle 摄像头句柄
 * @param[out] w 宽度
 * @param[out] h 高度
 * @return 0 成功
 */
int dal_camera_get_resolution(dal_camera_handle_t handle,
                              uint16_t *w, uint16_t *h);

#ifdef __cplusplus
}
#endif

#endif /* DAL_CAMERA_H */
