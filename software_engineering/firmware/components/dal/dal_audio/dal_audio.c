/**
 * @file    dal_audio.c
 * @brief   DAL 音频实现 — I2S Master + ES8311 CODEC
 */
#include "dal_audio.h"
#include "es8311.h"
#include "pal_i2c.h"
#include "pal_gpio.h"
#include "pal_log.h"
#include "driver/i2s_std.h"
#include <stdlib.h>
#include <string.h>

#define TAG "DAL_AUDIO"

typedef struct {
    es8311_handle_t       codec;
    pal_i2c_dev_handle_t  i2c_dev;
    i2s_chan_handle_t     tx_chan;
    dal_audio_config_t    cfg;
    bool                  inited;
} dal_audio_internal_t;

int dal_audio_init(dal_audio_handle_t *handle, const dal_audio_config_t *cfg)
{
    if (!handle || !cfg || !cfg->i2c_bus_handle) return -1;
    dal_audio_internal_t *c = calloc(1, sizeof(*c));
    if (!c) return -1;
    memcpy(&c->cfg, cfg, sizeof(*cfg));
    esp_err_t ret;

    /* 1. I2C 挂载 ES8311 (0x18) */
    pal_i2c_dev_config_t dev_cfg = { .device_address = 0x18, .scl_speed_hz = 400000 };
    ret = pal_i2c_dev_attach(&c->i2c_dev, (pal_i2c_bus_handle_t)cfg->i2c_bus_handle, &dev_cfg);
    if (ret) { free(c); return ret; }

    /* 2. ES8311 初始化 */
    ret = es8311_init(&c->codec, c->i2c_dev);
    if (ret) { pal_i2c_dev_detach(c->i2c_dev); free(c); return ret; }

    /* 2b. 外部 PA 使能 */
    if (cfg->pa_pin >= 0) {
        pal_gpio_set_direction(cfg->pa_pin, 1); /* OUTPUT */
        pal_gpio_write(cfg->pa_pin, 1);         /* HIGH = 功放开 */
        ESP_LOGI(TAG, "PA GPIO%d ON", cfg->pa_pin);
    }

    /* 3. I2S 初始化 */
    i2s_chan_config_t ch_cfg = I2S_CHANNEL_DEFAULT_CONFIG(cfg->i2s_port, I2S_ROLE_MASTER);
    ret = i2s_new_channel(&ch_cfg, &c->tx_chan, NULL);
    if (ret) goto fail;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = cfg->mclk_pin, .bclk = cfg->sclk_pin,
                      .ws = cfg->lclk_pin, .dout = cfg->dout_pin, .din = cfg->din_pin,
                      .invert_flags = { .mclk_inv=0,.bclk_inv=0,.ws_inv=0 } },
    };
    ret = i2s_channel_init_std_mode(c->tx_chan, &std_cfg);
    if (ret) goto fail;
    ret = i2s_channel_enable(c->tx_chan);
    if (ret) goto fail;

    c->inited = true; *handle = (dal_audio_handle_t)c;
    PAL_LOGI(TAG, "初始化完成 (%d Hz)", cfg->sample_rate); return 0;
fail:
    PAL_LOGE(TAG, "init fail: %d", ret);
    es8311_deinit(c->codec); pal_i2c_dev_detach(c->i2c_dev);
    if (c->tx_chan) { i2s_del_channel(c->tx_chan); }
    free(c); return ret;
}

int dal_audio_deinit(dal_audio_handle_t h) {
    dal_audio_internal_t *c = (dal_audio_internal_t *)h;
    if (!c || !c->inited) return -1;
    i2s_channel_disable(c->tx_chan); i2s_del_channel(c->tx_chan);
    es8311_deinit(c->codec); pal_i2c_dev_detach(c->i2c_dev);
    free(c); return 0;
}

int dal_audio_play(dal_audio_handle_t h, const int16_t *data, size_t len) {
    dal_audio_internal_t *c = (dal_audio_internal_t *)h;
    if (!c || !c->inited) return -1;
    size_t written; i2s_channel_write(c->tx_chan, data, len, &written, 1000);
    return (int)written;
}

int dal_audio_set_volume(dal_audio_handle_t h, uint8_t vol) {
    dal_audio_internal_t *c = (dal_audio_internal_t *)h;
    return c && c->inited ? es8311_set_volume(c->codec, vol) : -1;
}

bool dal_audio_is_busy(dal_audio_handle_t h) { return false; }
