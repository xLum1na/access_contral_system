/**
 * @file    es8311.h
 * @brief   ES8311 低功耗音频 CODEC 驱动（最小化 I2C 寄存器配置）
 *
 * 关键寄存器：RESET, CLK, SYS_PWR, DAC, VOLUME
 * I2C 地址：0x18 (7-bit)
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef ES8311_H
#define ES8311_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *pal_i2c_dev_handle_t;
typedef struct es8311_dev *es8311_handle_t;

/* ---- 寄存器地址 ---- */
#define ES8311_REG_RESET          0x00   /**< 复位 (写 0x1F=上电默认, 0x00=复位) */
#define ES8311_REG_CLK_MGR        0x01   /**< 时钟管理 */
#define ES8311_REG_SYS_PWR        0x02   /**< 系统电源控制 */
#define ES8311_REG_SERIAL_CTRL1   0x0A   /**< 串行数据控制1 (I2S 格式) */
#define ES8311_REG_SERIAL_CTRL2   0x0B   /**< 串行数据控制2 */
#define ES8311_REG_ADC_CTRL1      0x0D   /**< ADC 控制1 */
#define ES8311_REG_DAC_CTRL1      0x14   /**< DAC 控制1 */
#define ES8311_REG_DAC_CTRL2      0x15   /**< DAC 控制2 */
#define ES8311_REG_GPIO_CTRL1     0x17   /**< GPIO 控制1 (PA 使能) */
#define ES8311_REG_GPIO_CTRL2     0x18   /**< GPIO 控制2 */
#define ES8311_REG_VOLUME         0x2B   /**< DAC 音量 (0x00=-96dB, 0xC0=0dB) */

/** @brief 音量预设 */
#define ES8311_VOLUME_MIN         0x00   /**< 静音 */
#define ES8311_VOLUME_DEFAULT     0x80   /**< -48dB */
#define ES8311_VOLUME_MAX         0xC0   /**< 0dB (最大) */

/* ---- API ---- */
int es8311_init(es8311_handle_t *handle, pal_i2c_dev_handle_t i2c_dev);
int es8311_deinit(es8311_handle_t handle);
int es8311_set_volume(es8311_handle_t handle, uint8_t vol);
int es8311_mute(es8311_handle_t handle, bool mute);

#ifdef __cplusplus
}
#endif
#endif
