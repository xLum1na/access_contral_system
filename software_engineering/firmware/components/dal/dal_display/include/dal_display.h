/**
 * @file    dal_display.h
 * @brief   DAL 显示模块 — 统一显示抽象层
 *
 * 封装 MIPI DSI 显示子系统，协调以下硬件：
 *   - TC358762 MIPI DSI → RGB 桥接芯片（I2C 配置）
 *   - ATtiny88 背光 / 面板电源管理（I2C 通信）
 *   - ESP32-P4 MIPI DSI 主机（pal_mipi_dsi）
 *
 * 向上层（Service）提供统一的显示 API，屏蔽底层硬件细节。
 *
 * 典型使用流程：
 *   1. dal_display_init()     — 初始化所有子系统
 *   2. dal_display_on()       — 打开显示
 *   3. dal_display_set_backlight(80) — 设置背光 80%
 *   4. dal_display_fill()     — 清屏 / 填充
 *   5. dal_display_draw_bitmap() — 绘制位图
 *   6. dal_display_off()      — 关闭显示
 *   7. dal_display_deinit()   — 释放资源
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef DAL_DISPLAY_H
#define DAL_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "pal_mipi_dsi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 显示器不透明句柄 */
typedef void *dal_display_handle_t;

/* ================================================================
 *  配置结构体
 * ================================================================ */

/**
 * @brief 显示子系统完整配置
 *
 * 包含 DSI 总线、TC358762 桥接器、面板时序和背光的所有参数。
 */
typedef struct {
    /* ---- I2C 总线（TC358762 和 ATtiny88 共用） ---- */
    void     *i2c_bus_handle;            /**< PAL I2C 总线句柄（已初始化） */

    /* ---- TC358762 桥接器（寄存器通过 DSI Generic Write 配置，不使用 I2C） ---- */

    /* ---- ATtiny88 背光控制器 ---- */
    bool      use_attiny88;              /**< 是否使用 ATtiny88 管理背光 */
    uint16_t  attiny88_i2c_addr;         /**< ATtiny88 7 位 I2C 地址（0x45） */
    uint8_t   default_brightness;        /**< 默认亮度百分比 (0 ~ 100) */

    /* 上电时序（ms） */
    uint32_t  power_to_backlight_ms;     /**< 主电源稳定延时 (ms)，建议 ≥ 80 */

    /* ---- MIPI DSI 主机 ---- */
    int       dsi_host;                  /**< DSI 主机编号 */
    uint8_t   dsi_num_data_lanes;        /**< DSI 数据通道数（1 或 2） */
    int       dsi_lane_bit_rate_mbps;    /**< DSI 通道速率 (Mbps) */
    int       dsi_virtual_channel;       /**< DSI 虚拟通道 ID（0 ~ 3） */

    /* ---- 面板参数 ---- */
    uint16_t                   h_res;            /**< 水平有效像素 */
    uint16_t                   v_res;            /**< 垂直有效像素 */
    pal_mipi_dsi_color_fmt_t   pixel_format;     /**< DSI 输出像素格式 */
    pal_mipi_dsi_in_color_fmt_t in_color_format; /**< 帧缓冲颜色格式 */
    pal_mipi_dsi_timing_t      timing;              /**< 视频时序 */
    int                        num_fbs;              /**< 帧缓冲数量 */
    float                      dpi_clock_freq_mhz;   /**< DPI 像素时钟 (MHz) */

    /* ---- 背光 PWM（不使用 ATtiny88 时的备选方案） ---- */
    bool      bl_use_direct_pwm;          /**< 直接 PWM 背光（不使用 ATtiny88） */
    int       bl_gpio;                     /**< 背光 PWM GPIO */
    int       bl_ledc_channel;             /**< 背光 LEDC 通道 */
    int       bl_ledc_timer;               /**< 背光 LEDC 定时器 */
    uint32_t  bl_freq_hz;                  /**< 背光 PWM 频率 */
} dal_display_config_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

/**
 * @brief 初始化显示子系统（TC358762 → ATtiny88 → DSI 主机）
 *
 * 执行流程：
 *   1. 初始化 TC358762 桥接器（I2C 配置 + 复位）
 *   2. 初始化 ATtiny88 背光控制（若启用）
 *   3. 初始化 MIPI DSI 主机 + DPI 面板
 *   4. 等待面板电源就绪
 *
 * @param[out] handle 返回的显示器句柄
 * @param[in]  cfg    显示子系统配置
 * @return 0 成功，负数失败
 */
int dal_display_init(dal_display_handle_t *handle,
                     const dal_display_config_t *cfg);

/**
 * @brief 反初始化显示子系统，释放全部资源
 *
 * 执行逆序：停 DSI → 关背光 → 面板下电 → 释放设备
 *
 * @param handle 显示器句柄
 * @return 0 成功
 */
int dal_display_deinit(dal_display_handle_t handle);

/* ================================================================
 *  显示控制 API
 * ================================================================ */

/**
 * @brief 打开显示输出（面板上电 → 背光开 → DSI 开始扫描）
 *
 * 包含完整的上电时序。
 *
 * @param handle 显示器句柄
 * @return 0 成功
 */
int dal_display_on(dal_display_handle_t handle);

/**
 * @brief 关闭显示输出（DSI 停扫描 → 背光关 → 面板下电）
 *
 * @param handle 显示器句柄
 * @return 0 成功
 */
int dal_display_off(dal_display_handle_t handle);

/* ================================================================
 *  绘制 API
 * ================================================================ */

/**
 * @brief 在指定矩形区域绘制位图
 *
 * @param handle 显示器句柄
 * @param x      起始 X 坐标（像素）
 * @param y      起始 Y 坐标（像素）
 * @param w      宽度（像素）
 * @param h      高度（像素）
 * @param data   像素数据（格式与 in_color_format 匹配）
 * @return 0 成功，负数失败
 */
int dal_display_draw_bitmap(dal_display_handle_t handle,
                            uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h,
                            const void *data);

/**
 * @brief 用纯色填充矩形区域
 *
 * @param handle 显示器句柄
 * @param x      起始 X 坐标
 * @param y      起始 Y 坐标
 * @param w      宽度
 * @param h      高度
 * @param color  填充颜色（格式与 in_color_format 匹配）
 * @return 0 成功
 */
int dal_display_fill(dal_display_handle_t handle,
                     uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h,
                     uint32_t color);

/**
 * @brief 获取帧缓冲指针（供 Service 层直接渲染）
 *
 * @param handle 显示器句柄
 * @param[out] fb0 帧缓冲 0 的指针
 * @param[out] fb1 帧缓冲 1 的指针（双缓冲时有效，可为 NULL）
 * @return 0 成功
 */
int dal_display_get_fb(dal_display_handle_t handle,
                       void **fb0, void **fb1);

/* ================================================================
 *  背光 API
 * ================================================================ */

/**
 * @brief 设置背光亮度
 *
 * @param handle  显示器句柄
 * @param percent 亮度百分比（0 = 关，100 = 最亮）
 * @return 0 成功
 */
int dal_display_set_backlight(dal_display_handle_t handle, uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif /* DAL_DISPLAY_H */
