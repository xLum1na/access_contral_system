/**
 * @file    dal_pir.h
 * @brief   DAL 人体红外感应模块 — HC-SR501 驱动抽象
 *
 * 封装 HC-SR501 PIR 传感器，通过 GPIO 中断检测人体移动，
 * 向上层提供运动状态变化回调，用于系统唤醒/休眠调度。
 *
 * 硬件连接：
 *   - VCC → 3.3V（或 5V 经电平转换）
 *   - GND → GND
 *   - OUT → GPIO（配置为输入 + 内部下拉）
 *
 * HC-SR501 特性：
 *   - 上电后约 60 秒初始化稳定期
 *   - 检测到人体移动 → OUT 高电平
 *   - 移动停止后延时 → OUT 低电平（延时 2.5s~5min 可调，由板上电位器设定）
 *
 * @author  xiamu
 * @version 1.0
 */

#ifndef DAL_PIR_H
#define DAL_PIR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  类型定义
 * ================================================================ */

typedef void *dal_pir_handle_t;

/** @brief PIR 运动状态 */
typedef enum {
    DAL_PIR_STATE_IDLE   = 0,   /**< 无人（OUT 低电平） */
    DAL_PIR_STATE_MOTION = 1,   /**< 检测到移动（OUT 高电平） */
} dal_pir_state_t;

/**
 * @brief PIR 状态变化回调
 *
 * @param state     当前运动状态
 * @param arg       用户注册参数
 *
 * @warning 若回调源自 ISR 上下文，务必保持简短，仅做标志位/队列操作
 */
typedef void (*dal_pir_state_cb_t)(dal_pir_state_t state, void *arg);

/* ================================================================
 *  配置结构体
 * ================================================================ */

typedef struct {
    int gpio_pin;           /**< PIR OUT 引脚（GPIO 编号） */
    bool pull_down;         /**< 是否使能内部下拉（HC-SR501 推荐 true） */
} dal_pir_config_t;

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 初始化 PIR 传感器
 *
 * 配置 GPIO 为输入 + 可选内部下拉，注册双边沿中断。
 * 初始化完成后需调用 dal_pir_set_callback() 注册状态变化回调。
 *
 * @param[out] handle 输出句柄
 * @param[in]  cfg    引脚配置
 *
 * @return 0 成功，负数错误码
 *
 * @note   HC-SR501 上电有约 60s 初始化期，期间 OUT 可能不稳定。
 *         建议上电后延时 60s 再依赖传感器输出。
 */
int dal_pir_init(dal_pir_handle_t *handle, const dal_pir_config_t *cfg);

/**
 * @brief 反初始化 PIR 传感器
 *
 * 移除 GPIO 中断，释放资源。
 *
 * @param handle 句柄
 * @return 0 成功，负数错误码
 */
int dal_pir_deinit(dal_pir_handle_t handle);

/**
 * @brief 注册状态变化回调
 *
 * @param handle 句柄
 * @param cb     回调函数
 * @param arg    用户参数（传 NULL 则传入句柄自身）
 *
 * @return 0 成功，负数错误码
 */
int dal_pir_set_callback(dal_pir_handle_t handle, dal_pir_state_cb_t cb, void *arg);

/**
 * @brief 读取当前运动状态（非阻塞、无中断）
 *
 * @param handle 句柄
 * @param[out] state 当前状态
 *
 * @return 0 成功，负数错误码
 */
int dal_pir_get_state(dal_pir_handle_t handle, dal_pir_state_t *state);

/**
 * @brief 使能 PIR 中断检测
 *
 * @param handle 句柄
 * @return 0 成功，负数错误码
 */
int dal_pir_enable(dal_pir_handle_t handle);

/**
 * @brief 禁用 PIR 中断检测
 *
 * @param handle 句柄
 * @return 0 成功，负数错误码
 */
int dal_pir_disable(dal_pir_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* DAL_PIR_H */
