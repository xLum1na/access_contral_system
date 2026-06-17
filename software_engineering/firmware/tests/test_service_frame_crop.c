/**
 * @file    test_service_frame_crop.c
 * @brief   service_frame_crop 模块单元测试（宿主机测试）
 *
 * 测试覆盖 RAW8/RGB565 裁剪、越界处理、margin 外扩、不支持格式和
 * buffer 释放行为。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#include "unity.h"

#include "service_frame_crop.h"

#include <string.h>

#ifndef V4L2_PIX_FMT_SBGGR8
#define v4l2_fourcc(a, b, c, d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define V4L2_PIX_FMT_SBGGR8 v4l2_fourcc('B', 'A', '8', '1')
#define V4L2_PIX_FMT_RGB565 v4l2_fourcc('R', 'G', 'B', 'P')
#define V4L2_PIX_FMT_JPEG   v4l2_fourcc('J', 'P', 'E', 'G')
#endif

void setUp(void)
{
}

void tearDown(void)
{
}

/* ================================================================
 *  参数校验
 * ================================================================ */

void test_crop_init_null_handle_should_fail(void)
{
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_ERR_PARAM, service_frame_crop_init(NULL, NULL));
}

void test_crop_run_null_output_should_fail(void)
{
    service_frame_crop_handle_t handle = NULL;

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_init(&handle, NULL));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_ERR_PARAM,
                      service_frame_crop_run(handle, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_deinit(handle));
}

/* ================================================================
 *  RAW8 / RGB565 基础裁剪
 * ================================================================ */

void test_crop_raw8_center_2x2_should_copy_pixels(void)
{
    uint8_t image[16] = {
        0,  1,  2,  3,
        4,  5,  6,  7,
        8,  9, 10, 11,
        12, 13, 14, 15,
    };
    const dal_camera_frame_t frame = {
        .data = image,
        .size = sizeof(image),
        .width = 4,
        .height = 4,
        .pixelformat = V4L2_PIX_FMT_SBGGR8,
        .sequence = 7,
        .timestamp_us = 1234,
    };
    const service_face_box_t face = {
        .x1 = 1,
        .y1 = 1,
        .x2 = 3,
        .y2 = 3,
    };
    const service_frame_crop_config_t cfg = {
        .default_margin_percent = 0,
        .min_crop_width = 0,
        .min_crop_height = 0,
        .clamp_to_frame = true,
        .prefer_psram = false,
    };
    const uint8_t expected[4] = {5, 6, 9, 10};
    service_frame_crop_handle_t handle = NULL;
    service_frame_crop_image_t crop;

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_run(handle, &frame, &face, &crop));
    TEST_ASSERT_NOT_NULL(crop.data);
    TEST_ASSERT_EQUAL_UINT32(2, crop.width);
    TEST_ASSERT_EQUAL_UINT32(2, crop.height);
    TEST_ASSERT_EQUAL_UINT32(2, crop.stride_bytes);
    TEST_ASSERT_EQUAL_size_t(4, crop.size);
    TEST_ASSERT_EQUAL_UINT32(1, crop.x);
    TEST_ASSERT_EQUAL_UINT32(1, crop.y);
    TEST_ASSERT_EQUAL_UINT32(7, crop.source_sequence);
    TEST_ASSERT_EQUAL_UINT64(1234, crop.source_timestamp_us);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, crop.data, sizeof(expected));

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_release(handle, &crop));
    TEST_ASSERT_NULL(crop.data);
    TEST_ASSERT_EQUAL_size_t(0, crop.size);
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_deinit(handle));
}

void test_crop_rgb565_2x2_should_copy_pixels(void)
{
    uint16_t image[16] = {
        0x0000, 0x0001, 0x0002, 0x0003,
        0x0010, 0x0011, 0x0012, 0x0013,
        0x0020, 0x0021, 0x0022, 0x0023,
        0x0030, 0x0031, 0x0032, 0x0033,
    };
    const dal_camera_frame_t frame = {
        .data = image,
        .size = sizeof(image),
        .width = 4,
        .height = 4,
        .pixelformat = V4L2_PIX_FMT_RGB565,
    };
    const service_face_box_t face = {
        .x1 = 1,
        .y1 = 1,
        .x2 = 3,
        .y2 = 3,
    };
    const service_frame_crop_config_t cfg = {
        .clamp_to_frame = true,
        .prefer_psram = false,
    };
    const uint16_t expected[4] = {0x0011, 0x0012, 0x0021, 0x0022};
    service_frame_crop_handle_t handle = NULL;
    service_frame_crop_image_t crop;

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_run(handle, &frame, &face, &crop));
    TEST_ASSERT_EQUAL_UINT32(2, crop.width);
    TEST_ASSERT_EQUAL_UINT32(2, crop.height);
    TEST_ASSERT_EQUAL_UINT32(4, crop.stride_bytes);
    TEST_ASSERT_EQUAL_size_t(8, crop.size);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(expected, crop.data, 4);

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_release(handle, &crop));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_deinit(handle));
}

/* ================================================================
 *  边界 / margin / 格式
 * ================================================================ */

void test_crop_out_of_range_should_clamp_when_enabled(void)
{
    uint8_t image[16] = {
        0,  1,  2,  3,
        4,  5,  6,  7,
        8,  9, 10, 11,
        12, 13, 14, 15,
    };
    const dal_camera_frame_t frame = {
        .data = image,
        .size = sizeof(image),
        .width = 4,
        .height = 4,
        .pixelformat = V4L2_PIX_FMT_SBGGR8,
    };
    const service_face_box_t face = {
        .x1 = -1,
        .y1 = -1,
        .x2 = 2,
        .y2 = 2,
    };
    const service_frame_crop_config_t cfg = {
        .clamp_to_frame = true,
        .prefer_psram = false,
    };
    const uint8_t expected[4] = {0, 1, 4, 5};
    service_frame_crop_handle_t handle = NULL;
    service_frame_crop_image_t crop;

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_run(handle, &frame, &face, &crop));
    TEST_ASSERT_EQUAL_UINT32(2, crop.width);
    TEST_ASSERT_EQUAL_UINT32(2, crop.height);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, crop.data, sizeof(expected));

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_release(handle, &crop));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_deinit(handle));
}

void test_crop_out_of_range_should_fail_when_clamp_disabled(void)
{
    uint8_t image[16] = {0};
    const dal_camera_frame_t frame = {
        .data = image,
        .size = sizeof(image),
        .width = 4,
        .height = 4,
        .pixelformat = V4L2_PIX_FMT_SBGGR8,
    };
    const service_face_box_t face = {
        .x1 = -1,
        .y1 = -1,
        .x2 = 2,
        .y2 = 2,
    };
    const service_frame_crop_config_t cfg = {
        .clamp_to_frame = false,
        .prefer_psram = false,
    };
    service_frame_crop_handle_t handle = NULL;
    service_frame_crop_image_t crop;

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_ERR_OUT_OF_RANGE,
                      service_frame_crop_run(handle, &frame, &face, &crop));
    TEST_ASSERT_NULL(crop.data);
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_deinit(handle));
}

void test_crop_margin_should_expand_roi(void)
{
    uint8_t image[36] = {0};
    const dal_camera_frame_t frame = {
        .data = image,
        .size = sizeof(image),
        .width = 6,
        .height = 6,
        .pixelformat = V4L2_PIX_FMT_SBGGR8,
    };
    const service_face_box_t face = {
        .x1 = 2,
        .y1 = 2,
        .x2 = 4,
        .y2 = 4,
    };
    const service_frame_crop_config_t cfg = {
        .default_margin_percent = 50,
        .clamp_to_frame = true,
        .prefer_psram = false,
    };
    service_frame_crop_handle_t handle = NULL;
    service_frame_crop_image_t crop;

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_run(handle, &frame, &face, &crop));
    TEST_ASSERT_EQUAL_UINT32(4, crop.width);
    TEST_ASSERT_EQUAL_UINT32(4, crop.height);
    TEST_ASSERT_EQUAL_UINT32(1, crop.x);
    TEST_ASSERT_EQUAL_UINT32(1, crop.y);

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_release(handle, &crop));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_deinit(handle));
}

void test_crop_unsupported_format_should_fail_and_update_status(void)
{
    uint8_t image[16] = {0};
    const dal_camera_frame_t frame = {
        .data = image,
        .size = sizeof(image),
        .width = 4,
        .height = 4,
        .pixelformat = V4L2_PIX_FMT_JPEG,
    };
    const service_face_box_t face = {
        .x1 = 1,
        .y1 = 1,
        .x2 = 3,
        .y2 = 3,
    };
    service_frame_crop_handle_t handle = NULL;
    service_frame_crop_image_t crop;
    service_frame_crop_status_t status;

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_init(&handle, NULL));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_ERR_UNSUPPORTED_FMT,
                      service_frame_crop_run(handle, &frame, &face, &crop));
    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_get_status(handle, &status));
    TEST_ASSERT_EQUAL_UINT32(1, status.fail_count);
    TEST_ASSERT_EQUAL_UINT32(1, status.unsupported_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)SERVICE_FRAME_CROP_ERR_UNSUPPORTED_FMT, status.last_error);

    TEST_ASSERT_EQUAL(SERVICE_FRAME_CROP_OK, service_frame_crop_deinit(handle));
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_crop_init_null_handle_should_fail);
    RUN_TEST(test_crop_run_null_output_should_fail);
    RUN_TEST(test_crop_raw8_center_2x2_should_copy_pixels);
    RUN_TEST(test_crop_rgb565_2x2_should_copy_pixels);
    RUN_TEST(test_crop_out_of_range_should_clamp_when_enabled);
    RUN_TEST(test_crop_out_of_range_should_fail_when_clamp_disabled);
    RUN_TEST(test_crop_margin_should_expand_roi);
    RUN_TEST(test_crop_unsupported_format_should_fail_and_update_status);

    return UNITY_END();
}
