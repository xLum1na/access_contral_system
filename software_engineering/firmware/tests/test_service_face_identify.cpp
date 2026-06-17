/**
 * @file    test_service_face_identify.cpp
 * @brief   service_face_identify 模块单元测试（宿主机测试）
 *
 * 仅验证模型缺失、占位特征和 model-not-ready 安全行为，不接入真实
 * ESP-DL 推理或真实特征库。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

extern "C" {
#include "unity.h"
#include "service_face_identify.h"
#include "service_db.h"
}

#include <cstring>

static const char *MISSING_MODEL_PATH = "/tmp/face_feature_missing_for_unit_test.espdl";

int service_db_face_load_all(void)
{
    return DB_OK;
}

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

static service_face_box_t make_face(void)
{
    service_face_box_t face;

    memset(&face, 0, sizeof(face));
    face.x1 = 10;
    face.y1 = 10;
    face.x2 = 40;
    face.y2 = 40;
    face.score = 0.8f;
    return face;
}

void test_face_identify_init_param_should_fail(void)
{
    service_face_identify_config_t cfg;
    service_face_identify_handle_t handle = NULL;

    memset(&cfg, 0, sizeof(cfg));
    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_ERR_PARAM, service_face_identify_init(NULL, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_ERR_PARAM, service_face_identify_init(&handle, NULL));
}

void test_face_identify_missing_model_without_placeholder_should_fail(void)
{
    service_face_identify_config_t cfg;
    service_face_identify_handle_t handle = NULL;

    memset(&cfg, 0, sizeof(cfg));
    cfg.feature_model_path = MISSING_MODEL_PATH;
    cfg.model_id = 11;
    cfg.match_threshold = 0.72f;
    cfg.enable_placeholder = false;

    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_ERR_MODEL_NOT_FOUND,
                      service_face_identify_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

void test_face_identify_missing_model_with_placeholder_should_init(void)
{
    service_face_identify_config_t cfg;
    service_face_identify_status_t status;
    service_face_identify_handle_t handle = NULL;

    memset(&cfg, 0, sizeof(cfg));
    cfg.feature_model_path = MISSING_MODEL_PATH;
    cfg.model_id = 11;
    cfg.match_threshold = 0.72f;
    cfg.enable_placeholder = true;

    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_OK, service_face_identify_init(&handle, &cfg));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_OK, service_face_identify_get_status(handle, &status));
    TEST_ASSERT_FALSE(status.model_ready);
    TEST_ASSERT_TRUE(status.placeholder);
    TEST_ASSERT_EQUAL_UINT32(11, status.model_id);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.72f, status.match_threshold);
    TEST_ASSERT_EQUAL_STRING(MISSING_MODEL_PATH, status.model_path);
    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_OK, service_face_identify_deinit(handle));
}

void test_face_identify_placeholder_extract_should_output_fixed_feature(void)
{
    service_face_identify_config_t cfg;
    service_face_identify_handle_t handle = NULL;
    dal_camera_frame_t frame = make_frame();
    service_face_box_t face = make_face();
    uint8_t feature[16];
    uint32_t feature_size = 0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.feature_model_path = MISSING_MODEL_PATH;
    cfg.model_id = 11;
    cfg.enable_placeholder = true;

    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_OK, service_face_identify_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_OK,
                      service_face_identify_extract(handle, &frame, &face,
                                                    feature, sizeof(feature), &feature_size));
    TEST_ASSERT_EQUAL_UINT32(16, feature_size);
    for (uint32_t i = 0; i < feature_size; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(0xA0U + i), feature[i]);
    }
    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_OK, service_face_identify_deinit(handle));
}

void test_face_identify_placeholder_extract_small_buffer_should_fail(void)
{
    service_face_identify_config_t cfg;
    service_face_identify_handle_t handle = NULL;
    dal_camera_frame_t frame = make_frame();
    service_face_box_t face = make_face();
    uint8_t feature[15];
    uint32_t feature_size = 123;

    memset(&cfg, 0, sizeof(cfg));
    cfg.feature_model_path = MISSING_MODEL_PATH;
    cfg.model_id = 11;
    cfg.enable_placeholder = true;

    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_OK, service_face_identify_init(&handle, &cfg));
    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_ERR_PARAM,
                      service_face_identify_extract(handle, &frame, &face,
                                                    feature, sizeof(feature), &feature_size));
    TEST_ASSERT_EQUAL_UINT32(0, feature_size);
    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_OK, service_face_identify_deinit(handle));
}

void test_face_identify_placeholder_match_and_identify_should_not_match(void)
{
    service_face_identify_config_t cfg;
    service_face_identify_handle_t handle = NULL;
    dal_camera_frame_t frame = make_frame();
    service_face_box_t face = make_face();
    service_face_match_t match;
    const uint8_t feature[16] = {0};

    memset(&cfg, 0, sizeof(cfg));
    cfg.feature_model_path = MISSING_MODEL_PATH;
    cfg.model_id = 11;
    cfg.enable_placeholder = true;

    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_OK, service_face_identify_init(&handle, &cfg));

    memset(&match, 0xA5, sizeof(match));
    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_ERR_MODEL_NOT_READY,
                      service_face_identify_match(handle, feature, sizeof(feature), &match));
    TEST_ASSERT_FALSE(match.matched);
    TEST_ASSERT_EQUAL_UINT32(0, match.uid);

    memset(&match, 0xA5, sizeof(match));
    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_ERR_MODEL_NOT_READY,
                      service_face_identify_identify(handle, &frame, &face, &match));
    TEST_ASSERT_FALSE(match.matched);
    TEST_ASSERT_EQUAL_UINT32(0, match.uid);

    TEST_ASSERT_EQUAL(SERVICE_FACE_IDENTIFY_OK, service_face_identify_deinit(handle));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_face_identify_init_param_should_fail);
    RUN_TEST(test_face_identify_missing_model_without_placeholder_should_fail);
    RUN_TEST(test_face_identify_missing_model_with_placeholder_should_init);
    RUN_TEST(test_face_identify_placeholder_extract_should_output_fixed_feature);
    RUN_TEST(test_face_identify_placeholder_extract_small_buffer_should_fail);
    RUN_TEST(test_face_identify_placeholder_match_and_identify_should_not_match);

    return UNITY_END();
}
