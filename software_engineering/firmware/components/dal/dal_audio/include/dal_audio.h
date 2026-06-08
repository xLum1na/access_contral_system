/**
 * @file    dal_audio.h
 * @brief   DAL 音频模块 — I2S + ES8311 抽象层
 *
 * 硬件：ESP32-P4 I2S0 + ES8311 CODEC (I2C 0x18)
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef DAL_AUDIO_H
#define DAL_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *dal_audio_handle_t;

typedef struct {
    void   *i2c_bus_handle;   /**< I2C 总线句柄 */
    int     i2s_port;          /**< I2S 端口号 (0/1) */
    int     mclk_pin, sclk_pin, lclk_pin, dout_pin, din_pin;
    int     pa_pin;            /**< 外部功放 PA GPIO, -1 无 */
    int     sample_rate;       /**< 采样率 (8000/16000/22050/44100) */
} dal_audio_config_t;

int  dal_audio_init(dal_audio_handle_t *handle, const dal_audio_config_t *cfg);
int  dal_audio_deinit(dal_audio_handle_t handle);
int  dal_audio_play(dal_audio_handle_t handle, const int16_t *data, size_t len);
int  dal_audio_set_volume(dal_audio_handle_t handle, uint8_t vol);  /* 0-192 */
bool dal_audio_is_busy(dal_audio_handle_t handle);

#ifdef __cplusplus
}
#endif
#endif
