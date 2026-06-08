cmake_minimum_required(VERSION 3.16)
project(pal_test LANGUAGES C)

# ---- 构建选项 ----
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -g -O0")
# 宿主机测试不需要硬件相关定义，去除 ESP-IDF 宏
# 被测代码中有 SOC_LEDC_TIMER_NUM 等依赖，需提供最小定义
add_compile_definitions(
    SOC_LEDC_TIMER_NUM=4
    SOC_LEDC_CHANNEL_NUM=8
)

# ---- 源文件 ----
# Unity 框架
set(UNITY_SRCS
    unity/unity.c
)

# FFF 全局定义（仅此文件展开 DEFINE_FFF_GLOBALS）
set(FFF_SRCS
    fff_globals.c
)

# 被测 PAL 源码（从 components/pal/ 引用，不复制）
set(PAL_SRCS
    ${CMAKE_SOURCE_DIR}/../components/pal/pal_gpio/pal_gpio.c
)

# 测试用例
set(TEST_SRCS
    test_pal_gpio.c
)

# ---- 可执行文件 ----
add_executable(pal_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${PAL_SRCS}
    ${TEST_SRCS}
)

# ---- Include 路径 ----
# 关键：Mock 头文件路径在 PAL 路径之前，这样 #include "driver/gpio.h" 优先找到 mock
target_include_directories(pal_test PRIVATE
    ${CMAKE_SOURCE_DIR}/fff              # FFF 框架 (fff.h)
    ${CMAKE_SOURCE_DIR}/unity            # Unity 框架 (unity.h)
    ${CMAKE_SOURCE_DIR}/mocks            # Mock 头文件 (esp_err.h, driver/gpio.h, ...)
    ${CMAKE_SOURCE_DIR}/../components/pal/pal_gpio/include  # pal_gpio.h
)
