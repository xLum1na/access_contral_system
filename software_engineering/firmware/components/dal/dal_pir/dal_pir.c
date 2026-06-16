/**
 * @file    dal_pir.c
 * @brief   DAL 人体红外感应模块 — HC-SR501 驱动实现
 *
 * 参考文档：HC-SR501 数据手册
 * @author  xiamu
 * @version 1.0
 */

#include "dal_pir.h"
#include "pal_gpio.h"
#include "pal_log.h"

#include "esp_intr_alloc.h"

#include <stdlib.h>
#include <string.h>

#define TAG "DAL_PIR"

/* ================================================================
 *  内部数据结构
 * ================================================================ */

typedef struct {
    int                gpio;       /**< PIR 输出引脚 */
    dal_pir_state_t    state;      /**< 当前缓存状态 */
    dal_pir_state_cb_t cb;         /**< 状态变化回调 */
    void              *cb_arg;     /**< 回调用户参数 */
    bool               inited;     /**< 初始化完成标志 */
    bool               enabled;    /**< 中断使能状态 */
} dal_pir_internal_t;

/* ================================================================
 *  ISR
 * ================================================================ */

/**
 * @brief GPIO 双边沿 ISR
 *
 * 读取当前电平 → 更新状态 → 触发回调（若状态变化）。
 * 由于 HC-SR501 OUT 输出电平即是运动状态，可直接从电平推断。
 */
static void IRAM_ATTR dal_pir_isr_handler(void *arg)
{
    dal_pir_internal_t *ctx = (dal_pir_internal_t *)arg;
    if (!ctx || !ctx->enabled) return;

    int level = pal_gpio_read(ctx->gpio);
    dal_pir_state_t new_state = (level > 0) ? DAL_PIR_STATE_MOTION : DAL_PIR_STATE_IDLE;

    if (new_state != ctx->state) {
        ctx->state = new_state;
        if (ctx->cb) {
            ctx->cb(new_state, ctx->cb_arg ? ctx->cb_arg : ctx);
        }
    }
}

/* ================================================================
 *  Public API
 * ================================================================ */

int dal_pir_init(dal_pir_handle_t *handle, const dal_pir_config_t *cfg)
{
    if (!handle || !cfg) return -1;

    /* ---- 参数校验 ---- */
    if (cfg->gpio_pin < 0) {
        PAL_LOGE(TAG, "无效的 GPIO 引脚: %d", cfg->gpio_pin);
        return -1;
    }

    /* ---- 分配内部数据结构 ---- */
    dal_pir_internal_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        PAL_LOGE(TAG, "内存分配失败");
        return -1;
    }
    ctx->gpio     = cfg->gpio_pin;
    ctx->inited   = false;
    ctx->enabled  = false;

    /* ---- 配置 GPIO 为输入 + 可选下拉 ---- */
    int ret = pal_gpio_set_direction(cfg->gpio_pin, PAL_GPIO_DIR_INPUT);
    if (ret != 0) {
        PAL_LOGE(TAG, "GPIO%d 方向配置失败: %d", cfg->gpio_pin, ret);
        free(ctx);
        return ret;
    }

    if (cfg->pull_down) {
        ret = pal_gpio_set_pull_mode(cfg->gpio_pin, PAL_GPIO_PULL_DOWN);
        if (ret != 0) {
            PAL_LOGW(TAG, "GPIO%d 下拉配置失败: %d（继续初始化）", cfg->gpio_pin, ret);
        }
    }

    /* ---- 安装全局 GPIO ISR 服务（仅首次生效） ---- */
    ret = pal_gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != 0) {
        PAL_LOGE(TAG, "GPIO ISR 服务安装失败: %d", ret);
        free(ctx);
        return ret;
    }

    /* ---- 注册双边沿中断 ---- */
    ret = pal_gpio_set_intr(cfg->gpio_pin,
                            PAL_GPIO_INTR_ANYEDGE,
                            dal_pir_isr_handler,
                            ctx);
    if (ret != 0) {
        PAL_LOGE(TAG, "GPIO%d 中断注册失败: %d", cfg->gpio_pin, ret);
        free(ctx);
        return ret;
    }

    /* ---- 读取初始状态 ---- */
    int level = pal_gpio_read(cfg->gpio_pin);
    if (level > 0) {
        ctx->state = DAL_PIR_STATE_MOTION;
    } else {
        ctx->state = DAL_PIR_STATE_IDLE;
    }

    ctx->inited = true;
    *handle = (dal_pir_handle_t)ctx;
    PAL_LOGI(TAG, "初始化完成, GPIO%d, 初始状态=%s",
             cfg->gpio_pin,
             (ctx->state == DAL_PIR_STATE_MOTION) ? "有人" : "无人");
    return 0;
}

int dal_pir_deinit(dal_pir_handle_t handle)
{
    dal_pir_internal_t *ctx = (dal_pir_internal_t *)handle;
    if (!ctx || !ctx->inited) return -1;

    /* 先禁用中断再移除 */
    pal_gpio_remove_intr(ctx->gpio);

    ctx->inited  = false;
    ctx->enabled = false;
    free(ctx);
    PAL_LOGI(TAG, "已反初始化");
    return 0;
}

int dal_pir_set_callback(dal_pir_handle_t handle, dal_pir_state_cb_t cb, void *arg)
{
    dal_pir_internal_t *ctx = (dal_pir_internal_t *)handle;
    if (!ctx || !ctx->inited) return -1;

    ctx->cb     = cb;
    ctx->cb_arg = arg;
    return 0;
}

int dal_pir_get_state(dal_pir_handle_t handle, dal_pir_state_t *state)
{
    dal_pir_internal_t *ctx = (dal_pir_internal_t *)handle;
    if (!ctx || !ctx->inited || !state) return -1;

    /* 直接从硬件读取，不依赖缓存 */
    int level = pal_gpio_read(ctx->gpio);
    if (level > 0) {
        *state = DAL_PIR_STATE_MOTION;
    } else {
        *state = DAL_PIR_STATE_IDLE;
    }
    return 0;
}

int dal_pir_enable(dal_pir_handle_t handle)
{
    dal_pir_internal_t *ctx = (dal_pir_internal_t *)handle;
    if (!ctx || !ctx->inited) return -1;

    int ret = pal_gpio_intr_enable(ctx->gpio);
    if (ret == 0) {
        ctx->enabled = true;
    }
    return ret;
}

int dal_pir_disable(dal_pir_handle_t handle)
{
    dal_pir_internal_t *ctx = (dal_pir_internal_t *)handle;
    if (!ctx || !ctx->inited) return -1;

    int ret = pal_gpio_intr_disable(ctx->gpio);
    if (ret == 0) {
        ctx->enabled = false;
    }
    return ret;
}
