/**
 * @file    test_dal_relay.c
 * @brief   dal_relay 模块单元测试（宿主机 Mock 测试）
 *
 * 测试覆盖：
 *   - 参数校验
 *   - 初始化 GPIO 输出与初始电平
 *   - 开门 / 关门 / 状态读取
 *   - 底层 GPIO 错误透传
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#include "fff.h"
#include "unity.h"

/* ---- Mock 头文件（必须在 PAL/DAL 头文件之前包含） ---- */
#include "esp_err.h"
#include "driver/gpio.h"

/* ---- 被测模块 ---- */
#include "dal_relay.h"
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
}

void tearDown(void)
{
}

/* ================================================================
 *  参数校验
 * ================================================================ */

void test_relay_init_null_handle_should_fail(void)
{
    const dal_relay_config_t cfg = {
        .gpio_pin = 2,
        .active_level = 1,
        .init_open = false,
    };

    TEST_ASSERT_EQUAL(-1, dal_relay_init(NULL, &cfg));
}

void test_relay_init_null_config_should_fail(void)
{
    dal_relay_handle_t handle = NULL;

    TEST_ASSERT_EQUAL(-1, dal_relay_init(&handle, NULL));
    TEST_ASSERT_NULL(handle);
}

void test_relay_init_invalid_gpio_should_fail(void)
{
    dal_relay_handle_t handle = NULL;
    const dal_relay_config_t cfg = {
        .gpio_pin = -1,
        .active_level = 1,
        .init_open = false,
    };

    TEST_ASSERT_EQUAL(-1, dal_relay_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

void test_relay_init_invalid_active_level_should_fail(void)
{
    dal_relay_handle_t handle = NULL;
    const dal_relay_config_t cfg = {
        .gpio_pin = 2,
        .active_level = 2,
        .init_open = false,
    };

    TEST_ASSERT_EQUAL(-1, dal_relay_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

/* ================================================================
 *  初始化行为
 * ================================================================ */

void test_relay_init_active_high_closed_should_write_low(void)
{
    dal_relay_handle_t handle = NULL;
    const dal_relay_config_t cfg = {
        .gpio_pin = 2,
        .active_level = 1,
        .init_open = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_set_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_relay_init(&handle, &cfg));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(1, gpio_config_fake.call_count);
    TEST_ASSERT_EQUAL(1, gpio_set_level_fake.call_count);
    TEST_ASSERT_EQUAL(2, gpio_set_level_fake.arg0_val);
    TEST_ASSERT_EQUAL(0, gpio_set_level_fake.arg1_val);

    TEST_ASSERT_EQUAL(0, dal_relay_deinit(handle));
}

void test_relay_init_active_high_open_should_write_high(void)
{
    dal_relay_handle_t handle = NULL;
    const dal_relay_config_t cfg = {
        .gpio_pin = 2,
        .active_level = 1,
        .init_open = true,
    };

    gpio_config_fake.return_val = 0;
    gpio_set_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_relay_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(1, gpio_set_level_fake.arg1_val);

    TEST_ASSERT_EQUAL(0, dal_relay_deinit(handle));
}

void test_relay_init_active_low_closed_should_write_high(void)
{
    dal_relay_handle_t handle = NULL;
    const dal_relay_config_t cfg = {
        .gpio_pin = 2,
        .active_level = 0,
        .init_open = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_set_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_relay_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(1, gpio_set_level_fake.arg1_val);

    TEST_ASSERT_EQUAL(0, dal_relay_deinit(handle));
}

void test_relay_init_direction_fail_should_propagate_error(void)
{
    dal_relay_handle_t handle = NULL;
    const dal_relay_config_t cfg = {
        .gpio_pin = 2,
        .active_level = 1,
        .init_open = false,
    };

    gpio_config_fake.return_val = -7;

    TEST_ASSERT_EQUAL(-7, dal_relay_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

void test_relay_init_write_fail_should_propagate_error(void)
{
    dal_relay_handle_t handle = NULL;
    const dal_relay_config_t cfg = {
        .gpio_pin = 2,
        .active_level = 1,
        .init_open = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_set_level_fake.return_val = -8;

    TEST_ASSERT_EQUAL(-8, dal_relay_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

/* ================================================================
 *  状态控制
 * ================================================================ */

void test_relay_open_close_and_get_state(void)
{
    dal_relay_handle_t handle = NULL;
    dal_relay_state_t state = DAL_RELAY_STATE_CLOSED;
    const dal_relay_config_t cfg = {
        .gpio_pin = 2,
        .active_level = 1,
        .init_open = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_set_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_relay_init(&handle, &cfg));

    TEST_ASSERT_EQUAL(0, dal_relay_open(handle));
    TEST_ASSERT_EQUAL(1, gpio_set_level_fake.arg1_val);
    TEST_ASSERT_EQUAL(0, dal_relay_get_state(handle, &state));
    TEST_ASSERT_EQUAL(DAL_RELAY_STATE_OPEN, state);

    TEST_ASSERT_EQUAL(0, dal_relay_close(handle));
    TEST_ASSERT_EQUAL(0, gpio_set_level_fake.arg1_val);
    TEST_ASSERT_EQUAL(0, dal_relay_get_state(handle, &state));
    TEST_ASSERT_EQUAL(DAL_RELAY_STATE_CLOSED, state);

    TEST_ASSERT_EQUAL(0, dal_relay_deinit(handle));
}

void test_relay_open_write_fail_should_not_update_state(void)
{
    dal_relay_handle_t handle = NULL;
    dal_relay_state_t state = DAL_RELAY_STATE_OPEN;
    const dal_relay_config_t cfg = {
        .gpio_pin = 2,
        .active_level = 1,
        .init_open = false,
    };

    gpio_config_fake.return_val = 0;
    gpio_set_level_fake.return_val = 0;

    TEST_ASSERT_EQUAL(0, dal_relay_init(&handle, &cfg));

    gpio_set_level_fake.return_val = -9;
    TEST_ASSERT_EQUAL(-9, dal_relay_open(handle));
    TEST_ASSERT_EQUAL(0, dal_relay_get_state(handle, &state));
    TEST_ASSERT_EQUAL(DAL_RELAY_STATE_CLOSED, state);

    gpio_set_level_fake.return_val = 0;
    TEST_ASSERT_EQUAL(0, dal_relay_deinit(handle));
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_relay_init_null_handle_should_fail);
    RUN_TEST(test_relay_init_null_config_should_fail);
    RUN_TEST(test_relay_init_invalid_gpio_should_fail);
    RUN_TEST(test_relay_init_invalid_active_level_should_fail);

    RUN_TEST(test_relay_init_active_high_closed_should_write_low);
    RUN_TEST(test_relay_init_active_high_open_should_write_high);
    RUN_TEST(test_relay_init_active_low_closed_should_write_high);
    RUN_TEST(test_relay_init_direction_fail_should_propagate_error);
    RUN_TEST(test_relay_init_write_fail_should_propagate_error);

    RUN_TEST(test_relay_open_close_and_get_state);
    RUN_TEST(test_relay_open_write_fail_should_not_update_state);

    return UNITY_END();
}
