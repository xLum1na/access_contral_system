/**
 * @file    imx219.h
 * @brief   IMX219 摄像头传感器驱动（索尼背照式 CMOS）
 *
 * IMX219 是一款 8MP (3280×2464) MIPI CSI-2 图像传感器，支持：
 *   - RAW10 Bayer 输出
 *   - 2-lane / 4-lane MIPI
 *   - 通过 I2C (16-bit 寄存器地址) 配置
 *
 * 硬件特性：
 *   - I2C 地址：0x10
 *   - 像素尺寸：1.12 μm
 *   - 最大帧率：30fps@1080p, 15fps@full
 *
 * 参考文档：IMX219 数据手册 + Jetson IMX219 模块
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef IMX219_H
#define IMX219_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *pal_i2c_dev_handle_t;
typedef struct imx219_dev *imx219_handle_t;

/* ================================================================
 *  分辨率预设
 * ================================================================ */

typedef enum {
    IMX219_RES_640x480   = 0,   /**< VGA, 2×2+2×2 binning */
    IMX219_RES_1280x720  = 1,   /**< 720p, 2×2 binning */
    IMX219_RES_1920x1080 = 2,   /**< 1080p, 2×2 binning */
    IMX219_RES_3280x2464 = 3,   /**< Full, no binning */
} imx219_resolution_t;

/** @brief 分辨率对应的宽高 */
typedef struct {
    uint16_t width;
    uint16_t height;
} imx219_res_info_t;

/* ================================================================
 *  配置结构体
 * ================================================================ */

/**
 * @brief IMX219 初始化配置
 */
typedef struct {
    pal_i2c_dev_handle_t i2c_dev;         /**< I2C 设备句柄（地址 0x10） */
    imx219_resolution_t  resolution;       /**< 分辨率预设 */
    int                  fps;              /**< 目标帧率（15 或 30） */
    bool                 flip_horizontal;  /**< 水平翻转 */
    bool                 flip_vertical;    /**< 垂直翻转 */
    int                  xclk_pin;         /**< XVCLK 引脚 GPIO，-1 表示不使用（板载晶振） */
} imx219_config_t;

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 获取分辨率对应的宽高
 */
imx219_res_info_t imx219_res_info(imx219_resolution_t res);

/**
 * @brief 初始化 IMX219 传感器（I2C 寄存器配置序列 + 进入 streaming 状态）
 *
 * @param[out] handle 设备句柄
 * @param[in]  cfg    配置参数
 * @return 0 成功，负数失败
 */
int imx219_init(imx219_handle_t *handle, const imx219_config_t *cfg);

/**
 * @brief 反初始化（进入 standby 模式，释放资源）
 */
int imx219_deinit(imx219_handle_t handle);

/**
 * @brief 读取传感器 ID
 *
 * @param handle 设备句柄
 * @param[out] id 16-bit 芯片 ID (IMX219 = 0x0219)
 * @return 0 成功
 */
int imx219_read_id(imx219_handle_t handle, uint16_t *id);

/**
 * @brief 设置曝光时间（行数）
 *
 * @param handle     设备句柄
 * @param exposure_lines 曝光行数
 * @return 0 成功
 */
int imx219_set_exposure(imx219_handle_t handle, uint32_t exposure_lines);

/**
 * @brief 设置模拟增益
 *
 * @param handle 设备句柄
 * @param gain   增益值（原始寄存器值，0~480 对应 1x~16x）
 * @return 0 成功
 */
int imx219_set_gain(imx219_handle_t handle, uint16_t gain);

#ifdef __cplusplus
}
#endif

#endif /* IMX219_H */
