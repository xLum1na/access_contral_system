/**
 * @file    driver/gpio.h
 * @brief   Mock gpio.h — ESP-IDF GPIO 驱动（宿主机测试用）
 *
 * 使用 FFF (Fake Function Framework) 生成可控制的假函数。
 *
 * 包含规则：
 *   - 普通 .c 文件包含本头文件：仅 extern 声明（DECLARE_FAKE_*）
 *   - 定义 FFF_MOCK_DEFINITIONS 后包含：展开函数体和全局变量（FAKE_*）
 *     仅 fff_globals.c 应定义此宏。
 */

#ifndef MOCK_DRIVER_GPIO_H
#define MOCK_DRIVER_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "fff.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  类型定义（简化自 ESP-IDF hal/gpio_types.h）
 * ================================================================ */

typedef int gpio_num_t;

typedef enum {
    GPIO_INTR_DISABLE    = 0,
    GPIO_INTR_POSEDGE    = 1,
    GPIO_INTR_NEGEDGE    = 2,
    GPIO_INTR_ANYEDGE    = 3,
    GPIO_INTR_LOW_LEVEL  = 4,
    GPIO_INTR_HIGH_LEVEL = 5,
} gpio_int_type_t;

typedef enum {
    GPIO_MODE_INPUT         = 1,
    GPIO_MODE_OUTPUT        = 2,
    GPIO_MODE_INPUT_OUTPUT  = 3,
} gpio_mode_t;

typedef enum {
    GPIO_PULLUP_DISABLE      = 0x0,
    GPIO_PULLUP_ENABLE       = 0x1,
    GPIO_PULLDOWN_DISABLE    = 0x0,
    GPIO_PULLDOWN_ENABLE     = 0x2,
    GPIO_PULLUP_ONLY         = 0x1,
    GPIO_PULLDOWN_ONLY       = 0x2,
    GPIO_FLOATING            = 0x0,
} gpio_pull_mode_t;

typedef enum {
    GPIO_DRIVE_CAP_0 = 0,
    GPIO_DRIVE_CAP_1 = 1,
    GPIO_DRIVE_CAP_2 = 2,
    GPIO_DRIVE_CAP_3 = 3,
} gpio_drive_cap_t;

typedef struct {
    uint64_t         pin_bit_mask;
    gpio_mode_t      mode;
    gpio_pull_mode_t pull_up_en;
    gpio_pull_mode_t pull_down_en;
    gpio_int_type_t  intr_type;
} gpio_config_t;

/** @brief GPIO ISR 回调类型 */
typedef void (*gpio_isr_t)(void *arg);

/* ================================================================
 *  FFF Fake 函数 — 条件定义 / 声明
 * ================================================================ */

#ifdef FFF_MOCK_DEFINITIONS
/* ---- 函数体 + 全局变量定义（仅 fff_globals.c） ---- */
FAKE_VALUE_FUNC1(int, gpio_config, const gpio_config_t *);
FAKE_VALUE_FUNC2(int, gpio_set_pull_mode, gpio_num_t, gpio_pull_mode_t);
FAKE_VALUE_FUNC2(int, gpio_set_level, gpio_num_t, uint32_t);
FAKE_VALUE_FUNC1(int, gpio_get_level, gpio_num_t);
FAKE_VALUE_FUNC2(int, gpio_set_drive_capability, gpio_num_t, gpio_drive_cap_t);
FAKE_VALUE_FUNC1(int, gpio_install_isr_service, int);
FAKE_VOID_FUNC0(gpio_uninstall_isr_service);
FAKE_VALUE_FUNC3(int, gpio_isr_handler_add, gpio_num_t, gpio_isr_t, void *);
FAKE_VALUE_FUNC1(int, gpio_isr_handler_remove, gpio_num_t);
FAKE_VALUE_FUNC1(int, gpio_intr_enable, gpio_num_t);
FAKE_VALUE_FUNC1(int, gpio_intr_disable, gpio_num_t);
FAKE_VALUE_FUNC2(int, gpio_set_intr_type, gpio_num_t, gpio_int_type_t);
#else
/* ---- extern 声明（所有其他 .c 文件） ---- */
DECLARE_FAKE_VALUE_FUNC1(int, gpio_config, const gpio_config_t *);
DECLARE_FAKE_VALUE_FUNC2(int, gpio_set_pull_mode, gpio_num_t, gpio_pull_mode_t);
DECLARE_FAKE_VALUE_FUNC2(int, gpio_set_level, gpio_num_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC1(int, gpio_get_level, gpio_num_t);
DECLARE_FAKE_VALUE_FUNC2(int, gpio_set_drive_capability, gpio_num_t, gpio_drive_cap_t);
DECLARE_FAKE_VALUE_FUNC1(int, gpio_install_isr_service, int);
DECLARE_FAKE_VOID_FUNC0(gpio_uninstall_isr_service);
DECLARE_FAKE_VALUE_FUNC3(int, gpio_isr_handler_add, gpio_num_t, gpio_isr_t, void *);
DECLARE_FAKE_VALUE_FUNC1(int, gpio_isr_handler_remove, gpio_num_t);
DECLARE_FAKE_VALUE_FUNC1(int, gpio_intr_enable, gpio_num_t);
DECLARE_FAKE_VALUE_FUNC1(int, gpio_intr_disable, gpio_num_t);
DECLARE_FAKE_VALUE_FUNC2(int, gpio_set_intr_type, gpio_num_t, gpio_int_type_t);
#endif /* FFF_MOCK_DEFINITIONS */

#ifdef __cplusplus
}
#endif

#endif /* MOCK_DRIVER_GPIO_H */
