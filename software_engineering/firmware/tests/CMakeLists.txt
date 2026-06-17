cmake_minimum_required(VERSION 3.16)
project(pal_test LANGUAGES C CXX)

# ---- 构建选项 ----
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -g -O0")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -g -O0")
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

# ================================================================
#  service_db 测试
# ================================================================

add_executable(service_db_test
    ${UNITY_SRCS}
    ${CMAKE_SOURCE_DIR}/test_service_db.c
)

target_compile_definitions(service_db_test PRIVATE
    SOC_LEDC_TIMER_NUM=4
    SOC_LEDC_CHANNEL_NUM=8
)

target_include_directories(service_db_test PRIVATE
    ${CMAKE_SOURCE_DIR}/unity
    ${CMAKE_SOURCE_DIR}/mocks
    ${CMAKE_SOURCE_DIR}/../components/service/service_db
    ${CMAKE_SOURCE_DIR}/../components/service/service_db/include
    ${CMAKE_SOURCE_DIR}/../components/osal/include
)

# ================================================================
#  service_mqtt 测试
# ================================================================

set(SERVICE_MQTT_SRCS
    ${CMAKE_SOURCE_DIR}/../components/service/service_mqtt/service_mqtt.c
)

if(EXISTS ${SERVICE_MQTT_SRCS})
    add_executable(service_mqtt_test
        ${UNITY_SRCS}
        ${SERVICE_MQTT_SRCS}
        ${CMAKE_SOURCE_DIR}/test_service_mqtt.c
    )

    target_compile_definitions(service_mqtt_test PRIVATE
        SOC_LEDC_TIMER_NUM=4
        SOC_LEDC_CHANNEL_NUM=8
    )

    target_include_directories(service_mqtt_test PRIVATE
        ${CMAKE_SOURCE_DIR}/unity
        ${CMAKE_SOURCE_DIR}/mocks
        ${CMAKE_SOURCE_DIR}/../components/service/service_mqtt
        ${CMAKE_SOURCE_DIR}/../components/service/service_mqtt/include
        ${CMAKE_SOURCE_DIR}/../components/osal/include
        ${CMAKE_SOURCE_DIR}/../components/pal/pal_log/include
    )
else()
    message(WARNING "service_mqtt_test skipped: service_mqtt.c not found")
endif()

# ================================================================
#  dal_relay 测试
# ================================================================

add_executable(dal_relay_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${PAL_SRCS}
    ${CMAKE_SOURCE_DIR}/../components/dal/dal_relay/dal_relay.c
    ${CMAKE_SOURCE_DIR}/test_dal_relay.c
)

target_include_directories(dal_relay_test PRIVATE
    ${CMAKE_SOURCE_DIR}/fff
    ${CMAKE_SOURCE_DIR}/unity
    ${CMAKE_SOURCE_DIR}/mocks
    ${CMAKE_SOURCE_DIR}/../components/dal/dal_relay/include
    ${CMAKE_SOURCE_DIR}/../components/pal/pal_gpio/include
)

# ================================================================
#  dal_pir 测试
# ================================================================

add_executable(dal_pir_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${PAL_SRCS}
    ${CMAKE_SOURCE_DIR}/../components/dal/dal_pir/dal_pir.c
    ${CMAKE_SOURCE_DIR}/test_dal_pir.c
)

target_include_directories(dal_pir_test PRIVATE
    ${CMAKE_SOURCE_DIR}/fff
    ${CMAKE_SOURCE_DIR}/unity
    ${CMAKE_SOURCE_DIR}/mocks
    ${CMAKE_SOURCE_DIR}/../components/dal/dal_pir/include
    ${CMAKE_SOURCE_DIR}/../components/pal/pal_gpio/include
)

# ================================================================
#  service_user_manage 测试
# ================================================================

add_executable(service_user_manage_test
    ${UNITY_SRCS}
    ${CMAKE_SOURCE_DIR}/../components/service/service_user_manage/service_user_manage.c
    ${CMAKE_SOURCE_DIR}/test_service_user_manage.c
)

target_compile_definitions(service_user_manage_test PRIVATE
    SOC_LEDC_TIMER_NUM=4
    SOC_LEDC_CHANNEL_NUM=8
)

target_include_directories(service_user_manage_test PRIVATE
    ${CMAKE_SOURCE_DIR}/unity
    ${CMAKE_SOURCE_DIR}/mocks
    ${CMAKE_SOURCE_DIR}/../components/service/service_user_manage/include
    ${CMAKE_SOURCE_DIR}/../components/service/service_db/include
    ${CMAKE_SOURCE_DIR}/../components/service/service_face_detect/include
    ${CMAKE_SOURCE_DIR}/../components/service/service_face_identify/include
    ${CMAKE_SOURCE_DIR}/../components/dal/dal_camera/include
)

# ================================================================
#  service_face_detect 测试
# ================================================================

add_executable(service_face_detect_test
    ${UNITY_SRCS}
    ${CMAKE_SOURCE_DIR}/../components/service/service_face_detect/service_face_detect.cpp
    ${CMAKE_SOURCE_DIR}/test_service_face_detect.cpp
)

target_compile_definitions(service_face_detect_test PRIVATE
    SOC_LEDC_TIMER_NUM=4
    SOC_LEDC_CHANNEL_NUM=8
)

target_include_directories(service_face_detect_test PRIVATE
    ${CMAKE_SOURCE_DIR}/unity
    ${CMAKE_SOURCE_DIR}/mocks
    ${CMAKE_SOURCE_DIR}/../components/service/service_face_detect/include
    ${CMAKE_SOURCE_DIR}/../components/dal/dal_camera/include
    ${CMAKE_SOURCE_DIR}/../managed_components/espressif__esp-dl/vision/image
)

# ================================================================
#  service_face_identify 测试
# ================================================================

add_executable(service_face_identify_test
    ${UNITY_SRCS}
    ${CMAKE_SOURCE_DIR}/../components/service/service_face_identify/service_face_identify.cpp
    ${CMAKE_SOURCE_DIR}/test_service_face_identify.cpp
)

target_compile_definitions(service_face_identify_test PRIVATE
    SOC_LEDC_TIMER_NUM=4
    SOC_LEDC_CHANNEL_NUM=8
)

target_include_directories(service_face_identify_test PRIVATE
    ${CMAKE_SOURCE_DIR}/unity
    ${CMAKE_SOURCE_DIR}/mocks
    ${CMAKE_SOURCE_DIR}/../components/service/service_face_identify/include
    ${CMAKE_SOURCE_DIR}/../components/service/service_face_detect/include
    ${CMAKE_SOURCE_DIR}/../components/service/service_db/include
    ${CMAKE_SOURCE_DIR}/../components/dal/dal_camera/include
    ${CMAKE_SOURCE_DIR}/../managed_components/espressif__esp-dl/vision/image
)

