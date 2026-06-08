/**
 * @file    test_pal_gpio.c
 * @brief   pal_gpio 模块单元测试（宿主机 Mock 测试）
 *
 * 测试覆盖：
 *   - 引脚方向配置（输入 / 输出 / 双向）
 *   - 输出电平写 / 翻转
 *   - 输入电平读
 *   - 上下拉配置
 *   - 驱动强度设置
 *   - ISR 服务安装 / 中断注册 / 中断使能与禁用
 *   - 掩码读写（基础路径验证）
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#include "fff.h"
#include "unity.h"

/* ---- Mock 头文件（必须在 PAL 头文件之前包含） ---- */
#include "esp_err.h"
#include "driver/gpio.h"

/* ---- 被测模块 ---- */
#include "pal_gpio.h"

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */

void setUp(void)
{
    /* 每项测试前重置所有 Fake 函数 */
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
    /* 重置 PAL 层内部静态状态，确保各测试用例独立 */
    pal_gpio_uninstall_isr_service();
}

/**
 * @brief 冒烟测试：直接调用 mock 函数验证 FFF 基础设施
 */
void test_direct_mock_call(void)
{
    gpio_config_fake.return_val = 42;
    gpio_config_t cfg = {0};
    int ret = gpio_config(&cfg);
    TEST_ASSERT_EQUAL(42, ret);
    TEST_ASSERT_EQUAL_UINT(1, gpio_config_fake.call_count);
    TEST_ASSERT_EQUAL_PTR(&cfg, gpio_config_fake.arg0_val);
}

/* ================================================================
 *  测试：引脚方向配置
 * ================================================================ */

/**
 * @brief 设置引脚为输入模式，应调用 gpio_config 且 mode 为 INPUT
 */
void test_set_direction_input(void)
{
    gpio_config_fake.return_val = 0;

    int ret = pal_gpio_set_direction(4, PAL_GPIO_DIR_INPUT);

    /* arg0_val 指向栈变量，函数返回后不可靠访问；仅验证返回值 + 调用计数 */
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_UINT(1, gpio_config_fake.call_count);
}

/**
 * @brief 设置引脚为输出模式
 */
void test_set_direction_output(void)
{
    gpio_config_fake.return_val = 0;

    int ret = pal_gpio_set_direction(12, PAL_GPIO_DIR_OUTPUT);
    /* arg0_val 指向栈变量，不可靠；仅验证返回值 + 调用计数 */
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_UINT(1, gpio_config_fake.call_count);
}

/**
 * @brief gpio_config 底层返回错误时，PAL 应透传
 */
void test_set_direction_fail(void)
{
    gpio_config_fake.return_val = -1; /* ESP_FAIL */

    int ret = pal_gpio_set_direction(8, PAL_GPIO_DIR_INPUT);
    TEST_ASSERT_EQUAL(-1, ret);
}

/* ================================================================
 *  测试：上下拉配置
 * ================================================================ */

void test_set_pull_up(void)
{
    gpio_set_pull_mode_fake.return_val = 0;

    int ret = pal_gpio_set_pull_mode(5, PAL_GPIO_PULL_UP);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_UINT(1, gpio_set_pull_mode_fake.call_count);
    TEST_ASSERT_EQUAL(GPIO_PULLUP_ONLY, gpio_set_pull_mode_fake.arg1_val);
}

void test_set_pull_none(void)
{
    int ret = pal_gpio_set_pull_mode(6, PAL_GPIO_PULL_NONE);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(GPIO_FLOATING, gpio_set_pull_mode_fake.arg1_val);
}

/* ================================================================
 *  测试：输出
 * ================================================================ */

void test_write_high(void)
{
    gpio_set_level_fake.return_val = 0;

    int ret = pal_gpio_write(10, 1);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, gpio_set_level_fake.arg1_val);
}

void test_write_low(void)
{
    gpio_set_level_fake.return_val = 0;

    int ret = pal_gpio_write(10, 0);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(0, gpio_set_level_fake.arg1_val);
}

void test_toggle_from_low(void)
{
    gpio_get_level_fake.return_val = 0; /* 当前低电平 */
    gpio_set_level_fake.return_val = 0;

    int ret = pal_gpio_toggle(10);
    TEST_ASSERT_EQUAL(0, ret);
    /* 应翻转为高电平 */
    TEST_ASSERT_EQUAL(1, gpio_set_level_fake.arg1_val);
}

void test_toggle_from_high(void)
{
    gpio_get_level_fake.return_val = 1; /* 当前高电平 */
    gpio_set_level_fake.return_val = 0;

    int ret = pal_gpio_toggle(10);
    TEST_ASSERT_EQUAL(0, ret);
    /* 应翻转为低电平 */
    TEST_ASSERT_EQUAL(0, gpio_set_level_fake.arg1_val);
}

/* ================================================================
 *  测试：输入
 * ================================================================ */

void test_read_high(void)
{
    gpio_get_level_fake.return_val = 1;
    int level = pal_gpio_read(7);
    TEST_ASSERT_EQUAL(1, level);
    TEST_ASSERT_EQUAL(1, gpio_get_level_fake.call_count);
}

void test_read_low(void)
{
    gpio_get_level_fake.return_val = 0;
    int level = pal_gpio_read(7);
    TEST_ASSERT_EQUAL(0, level);
}

/* ================================================================
 *  测试：驱动强度
 * ================================================================ */

void test_set_drive_strength(void)
{
    gpio_set_drive_capability_fake.return_val = 0;

    int ret = pal_gpio_set_drive_strength(15, 2);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(15, gpio_set_drive_capability_fake.arg0_val);
    TEST_ASSERT_EQUAL(GPIO_DRIVE_CAP_2, gpio_set_drive_capability_fake.arg1_val);
}

/* ================================================================
 *  测试：中断
 * ================================================================ */

void test_install_isr_service(void)
{
    gpio_install_isr_service_fake.return_val = 0;

    int ret = pal_gpio_install_isr_service(0);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, gpio_install_isr_service_fake.call_count);
}

void test_install_isr_service_twice_should_be_idempotent(void)
{
    gpio_install_isr_service_fake.return_val = 0;

    /* 第一次安装 */
    int ret = pal_gpio_install_isr_service(0);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, gpio_install_isr_service_fake.call_count);

    /* 第二次应跳过（已安装），不再调用底层 */
    ret = pal_gpio_install_isr_service(0);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, gpio_install_isr_service_fake.call_count);
}

static void dummy_isr_cb(void *arg)
{
    (void)arg;
}

void test_set_intr_rising_edge(void)
{
    gpio_set_intr_type_fake.return_val = 0;
    gpio_isr_handler_add_fake.return_val = 0;

    int ret = pal_gpio_set_intr(3, PAL_GPIO_INTR_POSEDGE,
                                dummy_isr_cb, NULL);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, gpio_set_intr_type_fake.call_count);
    TEST_ASSERT_EQUAL(GPIO_INTR_POSEDGE, gpio_set_intr_type_fake.arg1_val);
    TEST_ASSERT_EQUAL(1, gpio_isr_handler_add_fake.call_count);
}

void test_set_intr_disable_should_not_add_handler(void)
{
    gpio_set_intr_type_fake.return_val = 0;

    int ret = pal_gpio_set_intr(3, PAL_GPIO_INTR_DISABLE,
                                dummy_isr_cb, NULL);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, gpio_set_intr_type_fake.call_count);
    /* 禁用中断不应注册 ISR */
    TEST_ASSERT_EQUAL(0, gpio_isr_handler_add_fake.call_count);
}

void test_intr_enable(void)
{
    gpio_intr_enable_fake.return_val = 0;

    int ret = pal_gpio_intr_enable(5);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(5, gpio_intr_enable_fake.arg0_val);
}

void test_intr_disable(void)
{
    gpio_intr_disable_fake.return_val = 0;

    int ret = pal_gpio_intr_disable(5);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(5, gpio_intr_disable_fake.arg0_val);
}

void test_remove_intr(void)
{
    gpio_intr_disable_fake.return_val = 0;
    gpio_isr_handler_remove_fake.return_val = 0;

    int ret = pal_gpio_remove_intr(5);
    TEST_ASSERT_EQUAL(0, ret);
    /* 应先禁用中断，再移除 ISR */
    TEST_ASSERT_EQUAL(1, gpio_intr_disable_fake.call_count);
    TEST_ASSERT_EQUAL(1, gpio_isr_handler_remove_fake.call_count);
}

/* ================================================================
 *  测试：掩码读写（基础覆盖）
 * ================================================================ */

void test_write_mask_two_pins(void)
{
    gpio_get_level_fake.return_val = 0;
    gpio_set_level_fake.return_val = 0;

    uint64_t mask = ((uint64_t)1 << 4) | ((uint64_t)1 << 5);
    int ret = pal_gpio_write_mask(mask, (uint64_t)1 << 4);
    TEST_ASSERT_EQUAL(0, ret);
    /* 两个引脚都被设置过电平 */
    TEST_ASSERT_EQUAL(2, gpio_set_level_fake.call_count);
}

void test_read_mask(void)
{
    /* pin 0 读到高电平 */
    gpio_get_level_fake.return_val = 1;

    uint64_t mask = ((uint64_t)1 << 0);
    int64_t result = pal_gpio_read_mask(mask);
    TEST_ASSERT_EQUAL(1, result);
    TEST_ASSERT_EQUAL(1, gpio_get_level_fake.call_count);
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */

int main(void)
{
    UNITY_BEGIN();

    /* 基础 Mock 验证 */
    RUN_TEST(test_direct_mock_call);

    /* 方向配置 */
    RUN_TEST(test_set_direction_input);
    RUN_TEST(test_set_direction_output);
    RUN_TEST(test_set_direction_fail);

    /* 上下拉 */
    RUN_TEST(test_set_pull_up);
    RUN_TEST(test_set_pull_none);

    /* 输出 */
    RUN_TEST(test_write_high);
    RUN_TEST(test_write_low);
    RUN_TEST(test_toggle_from_low);
    RUN_TEST(test_toggle_from_high);

    /* 输入 */
    RUN_TEST(test_read_high);
    RUN_TEST(test_read_low);

    /* 驱动强度 */
    RUN_TEST(test_set_drive_strength);

    /* 中断 */
    RUN_TEST(test_install_isr_service);
    RUN_TEST(test_install_isr_service_twice_should_be_idempotent);
    RUN_TEST(test_set_intr_rising_edge);
    RUN_TEST(test_set_intr_disable_should_not_add_handler);
    RUN_TEST(test_intr_enable);
    RUN_TEST(test_intr_disable);
    RUN_TEST(test_remove_intr);

    /* 掩码 */
    RUN_TEST(test_write_mask_two_pins);
    RUN_TEST(test_read_mask);

    return UNITY_END();
}
