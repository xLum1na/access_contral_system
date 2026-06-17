/**
 * @file    test_service_face_detect.cpp
 * @brief   service_face_detect 模块单元测试（宿主机测试）
 *
 * 仅验证模型缺失、占位模式和参数校验，不接入真实 ESP-DL 推理。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

extern "C" {
#include "unity.h"
#include "service_face_detect.h"
}

#include <cstring>

static const char *MISSING_MODEL_PATH = "/tmp/face_detect_missing_for_unit_test.espdl";

void setUp(void)
{
}

void tearDown(void)
{
}

static dal_camera_frame_t make_frame(void)
{
    static uint8_t image[100 * 80];
    dal_camera_frame_t frame;

    memset(image, 0, sizeof(image));
    memset(&frame, 0, sizeof(frame));
    frame.data = image;
    frame.size = sizeof(image);
    frame.width = 100;
    frame.height = 80;
    frame.pixelformat = 0;
    return frame;
}

void test_face_detect_init_param_should_fail(void)
{
    service_face_detect_config_t cfg;
    service_face_detect_handle_t handle = NULL;

    memset(&cfg, 0, sizeof(cfg));
    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_ERR_PARAM, service_face_detect_init(NULL, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_ERR_PARAM, service_face_detect_init(&handle, NULL));
}

void test_face_detect_missing_model_without_placeholder_should_fail(void)
{
    service_face_detect_config_t cfg;
    service_face_detect_handle_t handle = NULL;

    memset(&cfg, 0, sizeof(cfg));
    cfg.model_path = MISSING_MODEL_PATH;
    cfg.model_id = 7;
    cfg.enable_placeholder = false;

    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_ERR_MODEL_NOT_FOUND,
                      service_face_detect_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

void test_face_detect_missing_model_with_placeholder_should_init(void)
{
    service_face_detect_config_t cfg;
    service_face_detect_status_t status;
    service_face_detect_handle_t handle = NULL;

    memset(&cfg, 0, sizeof(cfg));
    cfg.model_path = MISSING_MODEL_PATH;
    cfg.model_id = 7;
    cfg.score_threshold = 0.6f;
    cfg.nms_threshold = 0.4f;
    cfg.enable_placeholder = true;

    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_OK, service_face_detect_init(&handle, &cfg));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_OK, service_face_detect_get_status(handle, &status));
    TEST_ASSERT_FALSE(status.model_ready);
    TEST_ASSERT_TRUE(status.placeholder);
    TEST_ASSERT_EQUAL_UINT32(7, status.model_id);
    TEST_ASSERT_EQUAL_STRING(MISSING_MODEL_PATH, status.model_path);
    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_OK, service_face_detect_deinit(handle));
}

void test_face_detect_placeholder_should_return_center_box(void)
{
    service_face_detect_config_t cfg;
    service_face_detect_handle_t handle = NULL;
    dal_camera_frame_t frame = make_frame();
    service_face_box_t boxes[1];
    int count = -1;

    memset(&cfg, 0, sizeof(cfg));
    cfg.model_path = MISSING_MODEL_PATH;
    cfg.model_id = 7;
    cfg.enable_placeholder = true;

    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_OK, service_face_detect_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_OK,
                      service_face_detect_run(handle, &frame, boxes, 1, &count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(37, boxes[0].x1);
    TEST_ASSERT_EQUAL(27, boxes[0].y1);
    TEST_ASSERT_EQUAL(63, boxes[0].x2);
    TEST_ASSERT_EQUAL(53, boxes[0].y2);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.50f, boxes[0].score);
    TEST_ASSERT_EQUAL(0, boxes[0].landmark_count);

    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_OK, service_face_detect_deinit(handle));
}

void test_face_detect_placeholder_without_output_boxes_should_return_zero_count(void)
{
    service_face_detect_config_t cfg;
    service_face_detect_handle_t handle = NULL;
    dal_camera_frame_t frame = make_frame();
    int count = -1;

    memset(&cfg, 0, sizeof(cfg));
    cfg.model_path = MISSING_MODEL_PATH;
    cfg.model_id = 7;
    cfg.enable_placeholder = true;

    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_OK, service_face_detect_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_OK,
                      service_face_detect_run(handle, &frame, NULL, 0, &count));
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_EQUAL(SERVICE_FACE_DETECT_OK, service_face_detect_deinit(handle));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_face_detect_init_param_should_fail);
    RUN_TEST(test_face_detect_missing_model_without_placeholder_should_fail);
    RUN_TEST(test_face_detect_missing_model_with_placeholder_should_init);
    RUN_TEST(test_face_detect_placeholder_should_return_center_box);
    RUN_TEST(test_face_detect_placeholder_without_output_boxes_should_return_zero_count);

    return UNITY_END();
}
