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
#define ES8311_REG_RESET          0x00   /**< 复位 / 模式控制 */
#define ES8311_REG_CLK_MGR1       0x01   /**< 时钟管理1 */
#define ES8311_REG_CLK_MGR2       0x02   /**< 时钟管理2 */
#define ES8311_REG_CLK_MGR3       0x03   /**< ADC OSR */
#define ES8311_REG_CLK_MGR4       0x04   /**< DAC OSR */
#define ES8311_REG_CLK_MGR5       0x05   /**< ADC/DAC 分频 */
#define ES8311_REG_CLK_MGR6       0x06   /**< BCLK 分频 */
#define ES8311_REG_CLK_MGR7       0x07   /**< LRCK 分频高位 */
#define ES8311_REG_CLK_MGR8       0x08   /**< LRCK 分频低位 */
#define ES8311_REG_SDP_IN         0x09   /**< DAC 串行输入端口 */
#define ES8311_REG_SDP_OUT        0x0A   /**< ADC 串行输出端口 */
#define ES8311_REG_SYS0B          0x0B   /**< 系统控制 */
#define ES8311_REG_SYS0C          0x0C   /**< 系统控制 */
#define ES8311_REG_SYS0D          0x0D   /**< 电源控制 */
#define ES8311_REG_SYS0E          0x0E   /**< 电源控制 */
#define ES8311_REG_SYS10          0x10   /**< 系统控制 */
#define ES8311_REG_SYS11          0x11   /**< 系统控制 */
#define ES8311_REG_SYS12          0x12   /**< DAC 使能 */
#define ES8311_REG_SYS13          0x13   /**< 系统控制 */
#define ES8311_REG_SYS14          0x14   /**< 系统控制 */
#define ES8311_REG_ADC15          0x15   /**< ADC 控制 */
#define ES8311_REG_ADC16          0x16   /**< ADC 增益 */
#define ES8311_REG_ADC17          0x17   /**< ADC 音量 */
#define ES8311_REG_ADC1B          0x1B   /**< ADC 高通 */
#define ES8311_REG_ADC1C          0x1C   /**< ADC 高通 */
#define ES8311_REG_DAC31          0x31   /**< DAC 静音控制 */
#define ES8311_REG_DAC32          0x32   /**< DAC 音量 */
#define ES8311_REG_DAC37          0x37   /**< DAC ramp */
#define ES8311_REG_GPIO44         0x44   /**< GPIO / 参考配置 */
#define ES8311_REG_GP45           0x45   /**< GP 控制 */
#define ES8311_REG_VOLUME         ES8311_REG_DAC32 /**< DAC 音量寄存器 */

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
