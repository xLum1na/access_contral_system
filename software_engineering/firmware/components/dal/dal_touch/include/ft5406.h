/**
 * @file    ft5406.h
 * @brief   FT5x06 系列电容触控控制器驱动
 *
 * FT5406 是 FocalTech 的电容触控 IC，支持最多 10 点触控。
 * 通过 I2C 接口（地址 0x38）通信，8-bit 寄存器地址。
 *
 * 同系列兼容：FT5206/FT5306/FT5406/FT5506
 *
 * 参考文档：FT5x06 数据手册
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef FT5406_H
#define FT5406_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *pal_i2c_dev_handle_t;
typedef struct ft5406_dev *ft5406_handle_t;

/* ---- 触控点上限 ---- */
#define FT5406_MAX_TOUCH_POINTS  5

/* ================================================================
 *  数据结构
 * ================================================================ */

/** @brief 单个触控点信息 */
typedef struct {
    uint16_t x;          /**< X 坐标 */
    uint16_t y;          /**< Y 坐标 */
    uint8_t  event;      /**< 0=down, 1=up, 2=contact */
    uint8_t  id;         /**< 触控点 ID (0~9) */
} ft5406_touch_point_t;

/** @brief 一帧触控数据 */
typedef struct {
    uint8_t              num_points;  /**< 当前触控点数 */
    ft5406_touch_point_t points[FT5406_MAX_TOUCH_POINTS];
} ft5406_touch_data_t;

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 初始化 FT5406
 *
 * @param[out] handle  设备句柄
 * @param      i2c_dev 已挂载的 I2C 设备句柄（地址 0x38）
 * @param      h_res   屏幕水平分辨率（用于坐标校验）
 * @param      v_res   屏幕垂直分辨率
 * @return 0 成功
 */
int ft5406_init(ft5406_handle_t *handle, pal_i2c_dev_handle_t i2c_dev,
                uint16_t h_res, uint16_t v_res);

/**
 * @brief 反初始化
 */
int ft5406_deinit(ft5406_handle_t handle);

/**
 * @brief 读取一帧触控数据（阻塞，直到有数据或超时）
 *
 * @param handle      设备句柄
 * @param[out] data   触控数据
 * @param timeout_ms  超时 (0=立即返回)
 * @return >0 触控点数，0 无触控，<0 错误
 */
int ft5406_read(ft5406_handle_t handle, ft5406_touch_data_t *data,
                uint32_t timeout_ms);

/**
 * @brief 读取芯片 ID
 *
 * @return 芯片 ID (FT5406 = 0x06)
 */
int ft5406_read_chip_id(ft5406_handle_t handle, uint8_t *id);

#ifdef __cplusplus
}
#endif

#endif /* FT5406_H */
