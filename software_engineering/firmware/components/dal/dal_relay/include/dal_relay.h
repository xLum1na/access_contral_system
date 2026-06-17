/**
 * @file    dal_relay.h
 * @brief   DAL 继电器模块 — 门锁/闸机继电器驱动抽象
 *
 * 封装通用继电器模块，通过 GPIO 输出电平控制继电器吸合/释放，
 * 用于人脸识别通过后开门等场景。
 *
 * 硬件连接：
 *   - VCC → 5V（继电器模块供电）
 *   - GND → GND
 *   - IN  → GPIO（控制引脚）
 *
 * 触发方式：
 *   - 高电平触发：IN = 1 → 吸合（开门），IN = 0 → 释放（关门）
 *   - 低电平触发：IN = 0 → 吸合（开门），IN = 1 → 释放（关门）
 *   由配置结构体 active_level 指定，适配不同继电器模块
 *
 * @author  xiamu
 * @version 1.0
 */

#ifndef DAL_RELAY_H
#define DAL_RELAY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  类型定义
 * ================================================================ */

typedef void *dal_relay_handle_t;

/** @brief 继电器物理状态 */
typedef enum {
    DAL_RELAY_STATE_CLOSED = 0,   /**< 释放（关门） */
    DAL_RELAY_STATE_OPEN   = 1,   /**< 吸合（开门） */
} dal_relay_state_t;

/* ================================================================
 *  配置结构体
 * ================================================================ */

typedef struct {
    int  gpio_pin;      /**< 继电器 IN 控制引脚（GPIO 编号） */
    int  active_level;  /**< 吸合（开门）对应的电平：1=高电平触发，0=低电平触发 */
    bool init_open;     /**< 初始化后是否立即吸合（true=开门，false=关门） */
} dal_relay_config_t;

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 初始化继电器
 *
 * 配置 GPIO 为推挽输出，并按 init_open 设置初始电平。
 *
 * @param[out] handle 输出句柄
 * @param[in]  cfg    引脚配置
 *
 * @return 0 成功，负数错误码
 */
int dal_relay_init(dal_relay_handle_t *handle, const dal_relay_config_t *cfg);

/**
 * @brief 反初始化继电器
 *
 * 释放前将继电器置为释放（关门）状态，再释放资源。
 *
 * @param handle 句柄
 * @return 0 成功，负数错误码
 */
int dal_relay_deinit(dal_relay_handle_t handle);

/**
 * @brief 吸合继电器（开门）
 *
 * @param handle 句柄
 * @return 0 成功，负数错误码
 */
int dal_relay_open(dal_relay_handle_t handle);

/**
 * @brief 释放继电器（关门）
 *
 * @param handle 句柄
 * @return 0 成功，负数错误码
 */
int dal_relay_close(dal_relay_handle_t handle);

/**
 * @brief 设置继电器状态
 *
 * @param handle 句柄
 * @param state  目标状态（OPEN / CLOSED）
 * @return 0 成功，负数错误码
 */
int dal_relay_set_state(dal_relay_handle_t handle, dal_relay_state_t state);

/**
 * @brief 读取继电器当前状态
 *
 * @param handle 句柄
 * @param[out] state 当前状态
 * @return 0 成功，负数错误码
 */
int dal_relay_get_state(dal_relay_handle_t handle, dal_relay_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* DAL_RELAY_H */
