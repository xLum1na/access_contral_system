/**
 * @file    test_dal_pir.c
 * @brief   dal_pir 模块单元测试（宿主机 Mock 测试）
 *
 * 测试覆盖：
 *   - 参数校验
 *   - GPIO 输入 / 下拉 / 中断注册
 *   - 中断使能与禁用
 *   - 运动状态读取
 *   - 回调注册
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#include "fff.h"
#include "unity.h"

/* ---- Mock 头文件（必须在 PAL/DAL 头文件之前包含） ---- */
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "driver/gpio.h"

/* ---- 被测模块 ---- */
#include "dal_pir.h"
#include "pal_gpio.h"

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */

void setUp(void)
{
    RESET_FAKE(gpio_config);
    RESET_FAKE(gpio_set_pull_mode);
    RESET_FAKE(gpio_set_level);
    RESET_FAKE(gpio_get_level);
    RESET_FAKE(gpio_set_drive_capability);
    RESET_FAKE(gpio_install_isr_service);
    RESET_FAKE(gpio_uninstall_isr_service);
    RESET_FAKE(gpio_isr_handler_add);
    RESET_FAKE(gpio_isr_handler_remove);
    RESET_FAKE(gpio_intr_enable);
    RESET_FAKE(gpio_intr_disable);
    RESET_FAKE(gpio_set_intr_type);

    pal_gpio_uninstall_isr_service();
}

void tearDown(void)
{
    pal_gpio_uninstall_isr_service();
}

static void test_pir_callback(dal_pir_state_t state, void *arg)
{
    (void)state;
    (void)arg;
}

/* ================================================================
 *  参数校验
 * ================================================================ */

void test_pir_init_null_handle_should_fail(void)
{
    const dal_pir_config_t cfg = {
        .gpio_pin = 1,
        .pull_down = true,
    };

    TEST_ASSERT_EQUAL(-1, dal_pir_init(NULL, &cfg));
}

void test_pir_init_null_config_should_fail(void)
{
    dal_pir_handle_t handle = NULL;

    TEST_ASSERT_EQUAL(-1, dal_pir_init(&handle, NULL));
    TEST_ASSERT_NULL(handle);
}

void test_pir_init_invalid_gpio_should_fail(void)
{
    dal_pir_handle_t handle = NULL;
    const dal_pir_config_t cfg = {
        .gpio_pin = -1,
        .pull_down = true,
    };

    TEST_ASSERT_EQUAL(-1, dal_pir_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

/* ================================================================
 *  初始化行为
 * ================================================================ */

void test_pir_init_with_pull_down_should_config_gpio_and_intr(void)
{
    dal_pir_handle_t handle = NULL;
    const dal_pir_config_t cfg = {
        .gpio_pin = 1,
        .pull_down = true,
    };

    gpio_config_fake.return_val = 0;
    gpio_set_pull_mode_fake.return_val = 0;
    gpio_install_isr_service_fake.return_val = 0;
    gpio_set_intr_type_fake.return_val = 0;
    gpio_isr_handler_add_fake.return_val = 0;
    gpio_get_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_pir_init(&handle, &cfg));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(1, gpio_config_fake.call_count);
    TEST_ASSERT_EQUAL(1, gpio_set_pull_mode_fake.call_count);
    TEST_ASSERT_EQUAL(GPIO_PULLDOWN_ONLY, gpio_set_pull_mode_fake.arg1_val);
    TEST_ASSERT_EQUAL(1, gpio_install_isr_service_fake.call_count);
    TEST_ASSERT_EQUAL(1, gpio_set_intr_type_fake.call_count);
    TEST_ASSERT_EQUAL(GPIO_INTR_ANYEDGE, gpio_set_intr_type_fake.arg1_val);
    TEST_ASSERT_EQUAL(1, gpio_isr_handler_add_fake.call_count);
    TEST_ASSERT_NOT_NULL(gpio_isr_handler_add_fake.arg1_val);
    TEST_ASSERT_NOT_NULL(gpio_isr_handler_add_fake.arg2_val);

    TEST_ASSERT_EQUAL(0, dal_pir_deinit(handle));
}

void test_pir_init_without_pull_down_should_not_config_pull(void)
{
    dal_pir_handle_t handle = NULL;
    const dal_pir_config_t cfg = {
        .gpio_pin = 1,
        .pull_down = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_install_isr_service_fake.return_val = 0;
    gpio_set_intr_type_fake.return_val = 0;
    gpio_isr_handler_add_fake.return_val = 0;
    gpio_get_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_pir_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(0, gpio_set_pull_mode_fake.call_count);

    TEST_ASSERT_EQUAL(0, dal_pir_deinit(handle));
}

void test_pir_init_direction_fail_should_propagate_error(void)
{
    dal_pir_handle_t handle = NULL;
    const dal_pir_config_t cfg = {
        .gpio_pin = 1,
        .pull_down = true,
    };

    gpio_config_fake.return_val = -3;

    TEST_ASSERT_EQUAL(-3, dal_pir_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

void test_pir_init_isr_service_fail_should_propagate_error(void)
{
    dal_pir_handle_t handle = NULL;
    const dal_pir_config_t cfg = {
        .gpio_pin = 1,
        .pull_down = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_install_isr_service_fake.return_val = -4;

    TEST_ASSERT_EQUAL(-4, dal_pir_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

/* ================================================================
 *  状态与中断控制
 * ================================================================ */

void test_pir_enable_disable_should_call_gpio_intr_api(void)
{
    dal_pir_handle_t handle = NULL;
    const dal_pir_config_t cfg = {
        .gpio_pin = 1,
        .pull_down = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_install_isr_service_fake.return_val = 0;
    gpio_set_intr_type_fake.return_val = 0;
    gpio_isr_handler_add_fake.return_val = 0;
    gpio_intr_enable_fake.return_val = 0;
    gpio_intr_disable_fake.return_val = 0;
    gpio_get_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_pir_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(0, dal_pir_enable(handle));
    TEST_ASSERT_EQUAL(1, gpio_intr_enable_fake.call_count);
    TEST_ASSERT_EQUAL(1, gpio_intr_enable_fake.arg0_val);

    TEST_ASSERT_EQUAL(0, dal_pir_disable(handle));
    TEST_ASSERT_EQUAL(1, gpio_intr_disable_fake.call_count);
    TEST_ASSERT_EQUAL(1, gpio_intr_disable_fake.arg0_val);

    TEST_ASSERT_EQUAL(0, dal_pir_deinit(handle));
}

void test_pir_get_state_should_return_motion_when_gpio_high(void)
{
    dal_pir_handle_t handle = NULL;
    dal_pir_state_t state = DAL_PIR_STATE_IDLE;
    const dal_pir_config_t cfg = {
        .gpio_pin = 1,
        .pull_down = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_install_isr_service_fake.return_val = 0;
    gpio_set_intr_type_fake.return_val = 0;
    gpio_isr_handler_add_fake.return_val = 0;
    gpio_get_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_pir_init(&handle, &cfg));

    gpio_get_level_fake.return_val = 1;
    TEST_ASSERT_EQUAL(0, dal_pir_get_state(handle, &state));
    TEST_ASSERT_EQUAL(DAL_PIR_STATE_MOTION, state);

    TEST_ASSERT_EQUAL(0, dal_pir_deinit(handle));
}

void test_pir_get_state_should_return_idle_when_gpio_low(void)
{
    dal_pir_handle_t handle = NULL;
    dal_pir_state_t state = DAL_PIR_STATE_MOTION;
    const dal_pir_config_t cfg = {
        .gpio_pin = 1,
        .pull_down = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_install_isr_service_fake.return_val = 0;
    gpio_set_intr_type_fake.return_val = 0;
    gpio_isr_handler_add_fake.return_val = 0;
    gpio_get_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_pir_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(0, dal_pir_get_state(handle, &state));
    TEST_ASSERT_EQUAL(DAL_PIR_STATE_IDLE, state);

    TEST_ASSERT_EQUAL(0, dal_pir_deinit(handle));
}

void test_pir_set_callback_should_succeed(void)
{
    dal_pir_handle_t handle = NULL;
    const dal_pir_config_t cfg = {
        .gpio_pin = 1,
        .pull_down = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_install_isr_service_fake.return_val = 0;
    gpio_set_intr_type_fake.return_val = 0;
    gpio_isr_handler_add_fake.return_val = 0;
    gpio_get_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_pir_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(0, dal_pir_set_callback(handle, test_pir_callback, NULL));

    TEST_ASSERT_EQUAL(0, dal_pir_deinit(handle));
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pir_init_null_handle_should_fail);
    RUN_TEST(test_pir_init_null_config_should_fail);
    RUN_TEST(test_pir_init_invalid_gpio_should_fail);

    RUN_TEST(test_pir_init_with_pull_down_should_config_gpio_and_intr);
    RUN_TEST(test_pir_init_without_pull_down_should_not_config_pull);
    RUN_TEST(test_pir_init_direction_fail_should_propagate_error);
    RUN_TEST(test_pir_init_isr_service_fail_should_propagate_error);

    RUN_TEST(test_pir_enable_disable_should_call_gpio_intr_api);
    RUN_TEST(test_pir_get_state_should_return_motion_when_gpio_high);
    RUN_TEST(test_pir_get_state_should_return_idle_when_gpio_low);
    RUN_TEST(test_pir_set_callback_should_succeed);

    return UNITY_END();
}
