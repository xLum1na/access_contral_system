/**
 * @file    dal_touch.h
 * @brief   DAL 触控模块 — 统一触控抽象层
 *
 * 封装 FT5x06 触控控制器，向上提供统一的触控 API。
 *
 * 硬件：
 *   - FT5406 触控控制器（I2C 0x38）
 *   - 树莓派 7" DSI 屏集成触控
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef DAL_TOUCH_H
#define DAL_TOUCH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *dal_touch_handle_t;

/** @brief 触控点信息 */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  event;   /**< 0=down, 1=up, 2=contact */
    uint8_t  id;
} dal_touch_point_t;

/** @brief 一帧触控数据 (最多 5 点) */
typedef struct {
    uint8_t           num_points;
    dal_touch_point_t points[5];
} dal_touch_data_t;

/* ---- 配置 ---- */
typedef struct {
    void     *i2c_bus_handle;   /**< I2C 总线句柄 */
    uint16_t  ft5406_i2c_addr;  /**< FT5406 地址 (0x38) */
    uint16_t  h_res;            /**< 屏幕宽 */
    uint16_t  v_res;            /**< 屏幕高 */
} dal_touch_config_t;

/* ---- API ---- */

int dal_touch_init(dal_touch_handle_t *handle, const dal_touch_config_t *cfg);
int dal_touch_deinit(dal_touch_handle_t handle);

/**
 * @brief 读取触控数据（非阻塞）
 * @return >0 触控点数, 0 无触控, <0 错误
 */
int dal_touch_read(dal_touch_handle_t handle, dal_touch_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* DAL_TOUCH_H */
