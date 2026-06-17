/**
 * @file    dal_relay.c
 * @brief   DAL 继电器模块 — 门锁/闸机继电器驱动实现
 *
 * 参考文档：通用继电器模块数据手册
 * @author  xiamu
 * @version 1.0
 */

#include "dal_relay.h"
#include "pal_gpio.h"
#include "pal_log.h"

#include <stdlib.h>
#include <string.h>

#define TAG "DAL_RELAY"

/* ================================================================
 *  内部数据结构
 * ================================================================ */

typedef struct {
    int                gpio;          /**< 控制引脚 */
    int                active_level;  /**< 吸合电平（0 或 1） */
    dal_relay_state_t  state;         /**< 当前缓存状态 */
    bool               inited;        /**< 初始化完成标志 */
} dal_relay_internal_t;

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/**
 * @brief 将继电器状态映射为 GPIO 输出电平
 *
 * 吸合（OPEN）时输出 active_level，释放（CLOSED）时输出其反相电平。
 */
static inline int dal_relay_state_to_level(dal_relay_internal_t *ctx,
                                           dal_relay_state_t state)
{
    if (state == DAL_RELAY_STATE_OPEN) {
        return ctx->active_level ? 1 : 0;
    }
    return ctx->active_level ? 0 : 1;
}

/* ================================================================
 *  Public API
 * ================================================================ */

int dal_relay_init(dal_relay_handle_t *handle, const dal_relay_config_t *cfg)
{
    if (!handle || !cfg) return -1;

    /* ---- 参数校验 ---- */
    if (cfg->gpio_pin < 0) {
        PAL_LOGE(TAG, "无效的 GPIO 引脚: %d", cfg->gpio_pin);
        return -1;
    }
    if (cfg->active_level != 0 && cfg->active_level != 1) {
        PAL_LOGE(TAG, "无效的 active_level: %d（应为 0 或 1）", cfg->active_level);
        return -1;
    }

    /* ---- 分配内部数据结构 ---- */
    dal_relay_internal_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        PAL_LOGE(TAG, "内存分配失败");
        return -1;
    }
    ctx->gpio         = cfg->gpio_pin;
    ctx->active_level = cfg->active_level;
    ctx->inited       = false;

    /* ---- 配置 GPIO 为推挽输出 ---- */
    int ret = pal_gpio_set_direction(cfg->gpio_pin, PAL_GPIO_DIR_OUTPUT);
    if (ret != 0) {
        PAL_LOGE(TAG, "GPIO%d 方向配置失败: %d", cfg->gpio_pin, ret);
        free(ctx);
        return ret;
    }

    /* ---- 设置初始电平（默认释放/关门，避免上电瞬间误开门） ---- */
    dal_relay_state_t init_state = cfg->init_open ? DAL_RELAY_STATE_OPEN
                                                  : DAL_RELAY_STATE_CLOSED;
    int level = dal_relay_state_to_level(ctx, init_state);
    ret = pal_gpio_write(cfg->gpio_pin, level);
    if (ret != 0) {
        PAL_LOGE(TAG, "GPIO%d 初始电平写入失败: %d", cfg->gpio_pin, ret);
        free(ctx);
        return ret;
    }
    ctx->state  = init_state;
    ctx->inited = true;
    *handle = (dal_relay_handle_t)ctx;

    PAL_LOGI(TAG, "初始化完成, GPIO%d, 触发电平=%s, 初始状态=%s",
             cfg->gpio_pin,
             cfg->active_level ? "高电平" : "低电平",
             (init_state == DAL_RELAY_STATE_OPEN) ? "开门" : "关门");
    return 0;
}

int dal_relay_deinit(dal_relay_handle_t handle)
{
    dal_relay_internal_t *ctx = (dal_relay_internal_t *)handle;
    if (!ctx || !ctx->inited) return -1;

    /* 释放前先关门，确保安全 */
    dal_relay_close(handle);
    ctx->inited = false;
    free(ctx);
    PAL_LOGI(TAG, "已反初始化");
    return 0;
}

int dal_relay_open(dal_relay_handle_t handle)
{
    return dal_relay_set_state(handle, DAL_RELAY_STATE_OPEN);
}

int dal_relay_close(dal_relay_handle_t handle)
{
    return dal_relay_set_state(handle, DAL_RELAY_STATE_CLOSED);
}

int dal_relay_set_state(dal_relay_handle_t handle, dal_relay_state_t state)
{
    dal_relay_internal_t *ctx = (dal_relay_internal_t *)handle;
    if (!ctx || !ctx->inited) return -1;

    if (state != DAL_RELAY_STATE_OPEN && state != DAL_RELAY_STATE_CLOSED) {
        PAL_LOGE(TAG, "无效的继电器状态: %d", state);
        return -1;
    }

    int level = dal_relay_state_to_level(ctx, state);
    int ret = pal_gpio_write(ctx->gpio, level);
    if (ret != 0) {
        PAL_LOGE(TAG, "GPIO%d 电平写入失败: %d", ctx->gpio, ret);
        return ret;
    }

    ctx->state = state;
    return 0;
}

int dal_relay_get_state(dal_relay_handle_t handle, dal_relay_state_t *state)
{
    dal_relay_internal_t *ctx = (dal_relay_internal_t *)handle;
    if (!ctx || !ctx->inited || !state) return -1;

    *state = ctx->state;
    return 0;
}
