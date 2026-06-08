/**
 * @file    attiny88.h
 * @brief   ATtiny88 辅助 MCU 驱动 — 背光 + 面板电源 + 复位管理
 *
 * ATtiny88 通过 I2C 从机模式与 ESP32-P4 通信（地址 0x45），负责：
 *   - 面板电源轨控制（VDD 主电源）
 *   - TC358762 桥接器硬件复位
 *   - LCD 背光 PWM（0 ~ 255）
 *   - FT5406 触控控制器复位
 *
 * 寄存器采用 8 位地址 + 8 位数据的简单 I2C 协议。
 *
 * 参考：树莓派 7" DSI 屏 ATTINY88 固件 + 硬件原理图
 * @author  Access System Firmware Team
 * @version 2.0
 */

#ifndef ATTINY88_H
#define ATTINY88_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *pal_i2c_dev_handle_t;
typedef struct attiny88_dev *attiny88_handle_t;

/* ================================================================
 *  寄存器定义（ATTINY88 固件 v2, ID=0xC3）
 * ================================================================ */

typedef enum {
    ATTINY88_REG_ID      = 0x80,   /**< 固件 ID (RO, 0xC3=v2) */
    ATTINY88_REG_PORTA   = 0x81,   /**< 扫描方向 (bit2=左→右) */
    ATTINY88_REG_PORTB   = 0x82,   /**< 主电源 (bit7=开) */
    ATTINY88_REG_PORTC   = 0x83,   /**< 复位控制 */
    ATTINY88_REG_PWM     = 0x86,   /**< 背光 PWM (0~255) */
} attiny88_reg_t;

/** @brief PORTA 位定义 */
#define ATTINY88_PORTA_SCAN_LR    (1 << 2)   /**< 扫描方向：左 → 右 */

/** @brief PORTB 位定义 */
#define ATTINY88_PORTB_POWER_ON   (1 << 7)   /**< 主电源开关 */

/** @brief PORTC 复位控制位 */
#define ATTINY88_PORTC_LED_EN     (1 << 0)   /**< 背光 LED 使能 */
#define ATTINY88_PORTC_TOUCH_RST  (1 << 1)   /**< 触控复位（低有效） */
#define ATTINY88_PORTC_LCD_RST    (1 << 2)   /**< LCD 复位（低有效） */
#define ATTINY88_PORTC_BRIDGE_RST (1 << 3)   /**< TC358762 桥复位（低有效） */

/** @brief 固件 ID */
#define ATTINY88_FW_ID_V2         0xC3

/* ================================================================
 *  配置结构体
 * ================================================================ */

/**
 * @brief ATtiny88 初始化配置
 */
typedef struct {
    pal_i2c_dev_handle_t i2c_dev;           /**< I2C 设备句柄（已挂载到 0x45） */
    uint32_t             power_on_delay_ms; /**< 主电源上电后稳定延时 (ms) */
    uint32_t             reset_release_delay_ms; /**< 释放复位后延时 (ms) */
    uint8_t              default_brightness;/**< 默认背光亮度 (0 ~ 255) */
} attiny88_config_t;

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 初始化 ATtiny88 — 读 ID 校验 + 设置初始状态
 *
 * @param[out] handle 设备句柄
 * @param[in]  cfg    配置
 * @return 0 成功，负数失败
 */
int attiny88_init(attiny88_handle_t *handle, const attiny88_config_t *cfg);

/**
 * @brief 反初始化
 */
int attiny88_deinit(attiny88_handle_t handle);

/**
 * @brief 主电源上电 + 释放所有复位
 *
 * 流程：PORTB 开主电源 → PORTC 释放桥/LCD/触控复位 → 延时
 *
 * @param handle 设备句柄
 * @return 0 成功
 */
int attiny88_power_on(attiny88_handle_t handle);

/**
 * @brief 关闭主电源（所有复位拉低）
 *
 * @param handle 设备句柄
 * @return 0 成功
 */
int attiny88_power_off(attiny88_handle_t handle);

/**
 * @brief 释放桥复位（在 DSI 总线就绪后调用）
 *
 * 释放 TC358762 桥 + LCD + 触控复位，使桥进入可配置状态。
 *
 * @param handle 设备句柄
 * @return 0 成功
 */
int attiny88_release_bridge_reset(attiny88_handle_t handle);

/**
 * @brief 设置背光亮度
 *
 * @param handle     设备句柄
 * @param brightness 亮度值 (0 = 关, 255 = 最亮)
 * @return 0 成功
 */
int attiny88_set_brightness(attiny88_handle_t handle, uint8_t brightness);

/**
 * @brief 获取当前亮度
 */
int attiny88_get_brightness(attiny88_handle_t handle, uint8_t *brightness);

/**
 * @brief 读取固件 ID
 *
 * @param handle 设备句柄
 * @param[out] id 固件 ID (0xC3 = v2)
 * @return 0 成功
 */
int attiny88_read_id(attiny88_handle_t handle, uint8_t *id);

/**
 * @brief 读取 PORTC 复位状态
 *
 * @param handle 设备句柄
 * @param[out] state PORTC 寄存器值
 * @return 0 成功
 */
int attiny88_read_state(attiny88_handle_t handle, uint8_t *state);

#ifdef __cplusplus
}
#endif

#endif /* ATTINY88_H */
