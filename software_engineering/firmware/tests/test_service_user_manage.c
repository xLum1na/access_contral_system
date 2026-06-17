/**
 * @file    test_service_user_manage.c
 * @brief   service_user_manage 模块单元测试（宿主机测试）
 *
 * 使用 mock service_db / face service，仅验证用户管理服务的基础 CRUD
 * 封装和本地权限检查逻辑。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#include "unity.h"

#include "service_user_manage.h"

#include <string.h>
#include <time.h>

#define MOCK_USER_MAX 8

static db_user_record_t s_users[MOCK_USER_MAX];
static uint32_t s_next_uid;
static uint32_t s_face_delete_uid;
static uint32_t s_user_delete_uid;

static void mock_reset(void)
{
    memset(s_users, 0, sizeof(s_users));
    s_next_uid = 1;
    s_face_delete_uid = 0;
    s_user_delete_uid = 0;
}

static int mock_find_index(uint32_t uid)
{
    for (int i = 0; i < MOCK_USER_MAX; i++) {
        if (s_users[i].uid == uid) return i;
    }
    return -1;
}

static int mock_add_user(const db_user_record_t *user, uint32_t uid)
{
    for (int i = 0; i < MOCK_USER_MAX; i++) {
        if (s_users[i].uid == 0) {
            s_users[i] = *user;
            s_users[i].uid = uid;
            return 0;
        }
    }
    return DB_ERR_FULL;
}

/* ================================================================
 *  Mock — service_db
 * ================================================================ */

int service_db_user_add(const db_user_record_t *user, uint32_t *out_uid)
{
    uint32_t uid;

    if (!user || !out_uid) return DB_ERR_PARAM;
    uid = s_next_uid++;
    if (mock_add_user(user, uid) != 0) return DB_ERR_FULL;
    *out_uid = uid;
    return DB_OK;
}

int service_db_user_get(uint32_t uid, db_user_record_t *user)
{
    int index;

    if (uid == 0 || !user) return DB_ERR_PARAM;
    index = mock_find_index(uid);
    if (index < 0) return DB_ERR_NOT_FOUND;
    *user = s_users[index];
    return DB_OK;
}

int service_db_user_update(uint32_t uid, const db_user_record_t *user)
{
    int index;

    if (uid == 0 || !user) return DB_ERR_PARAM;
    index = mock_find_index(uid);
    if (index < 0) return DB_ERR_NOT_FOUND;
    s_users[index] = *user;
    s_users[index].uid = uid;
    return DB_OK;
}

int service_db_user_delete(uint32_t uid)
{
    int index;

    if (uid == 0) return DB_ERR_PARAM;
    s_user_delete_uid = uid;
    index = mock_find_index(uid);
    if (index < 0) return DB_ERR_NOT_FOUND;
    memset(&s_users[index], 0, sizeof(s_users[index]));
    return DB_OK;
}

int service_db_user_list(db_user_record_t *list, int max, int *total)
{
    int count = 0;
    int copied = 0;

    if (!list || max <= 0) return DB_ERR_PARAM;
    for (int i = 0; i < MOCK_USER_MAX; i++) {
        if (s_users[i].uid != 0) {
            if (copied < max) {
                list[copied++] = s_users[i];
            }
            count++;
        }
    }
    if (total) *total = count;
    return DB_OK;
}

int service_db_face_delete(uint32_t uid)
{
    if (uid == 0) return DB_ERR_PARAM;
    s_face_delete_uid = uid;
    return DB_OK;
}

int service_db_face_set(uint32_t uid, const uint8_t *feature,
                        uint32_t size, uint32_t model_id)
{
    (void)uid;
    (void)feature;
    (void)size;
    (void)model_id;
    return DB_OK;
}

/* ================================================================
 *  Mock — face service
 * ================================================================ */

int service_face_detect_run(service_face_detect_handle_t handle,
                            const dal_camera_frame_t *frame,
                            service_face_box_t *boxes,
                            int max_boxes,
                            int *out_count)
{
    (void)handle;
    (void)frame;
    (void)boxes;
    (void)max_boxes;
    if (out_count) *out_count = 0;
    return SERVICE_FACE_DETECT_OK;
}

int service_face_identify_extract(service_face_identify_handle_t handle,
                                  const dal_camera_frame_t *frame,
                                  const service_face_box_t *face,
                                  uint8_t *feature,
                                  uint32_t feature_buf_size,
                                  uint32_t *out_feature_size)
{
    (void)handle;
    (void)frame;
    (void)face;
    if (!feature || feature_buf_size < 1 || !out_feature_size) return SERVICE_FACE_IDENTIFY_ERR_PARAM;
    feature[0] = 0xA5;
    *out_feature_size = 1;
    return SERVICE_FACE_IDENTIFY_OK;
}

int service_face_identify_identify(service_face_identify_handle_t handle,
                                   const dal_camera_frame_t *frame,
                                   const service_face_box_t *face,
                                   service_face_match_t *out_match)
{
    (void)handle;
    (void)frame;
    (void)face;
    if (!out_match) return SERVICE_FACE_IDENTIFY_ERR_PARAM;
    memset(out_match, 0, sizeof(*out_match));
    return SERVICE_FACE_IDENTIFY_ERR_MODEL_NOT_READY;
}

int service_face_identify_get_status(service_face_identify_handle_t handle,
                                     service_face_identify_status_t *status)
{
    (void)handle;
    if (!status) return SERVICE_FACE_IDENTIFY_ERR_PARAM;
    memset(status, 0, sizeof(*status));
    status->model_id = 1;
    return SERVICE_FACE_IDENTIFY_OK;
}

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */

void setUp(void)
{
}

void tearDown(void)
{
}

/* ================================================================
 *  测试用例
 * ================================================================ */

void test_user_manage_before_init_should_fail(void)
{
    bool allowed = true;

    mock_reset();
    TEST_ASSERT_EQUAL(-1, service_user_manage_create_user(NULL, NULL));
    TEST_ASSERT_EQUAL(-1, service_user_manage_check_permission(1, 0, 100, &allowed));
    TEST_ASSERT_FALSE(allowed);
}

void test_user_manage_create_get_update_list_should_work(void)
{
    service_user_create_req_t req;
    service_user_create_req_t update;
    db_user_record_t user;
    db_user_record_t list[2];
    uint32_t uid = 0;
    int total = 0;

    mock_reset();
    TEST_ASSERT_EQUAL(0, service_user_manage_init((service_face_detect_handle_t)0x1,
                                                  (service_face_identify_handle_t)0x2));

    memset(&req, 0, sizeof(req));
    strcpy(req.name, "alice");
    strcpy(req.pin, "1234");
    req.perm = 0x03;
    req.valid_from = 100;
    req.valid_until = 200;
    req.admin = false;
    TEST_ASSERT_EQUAL(0, service_user_manage_create_user(&req, &uid));
    TEST_ASSERT_EQUAL_UINT32(1, uid);

    memset(&user, 0, sizeof(user));
    TEST_ASSERT_EQUAL(0, service_user_manage_get_user(uid, &user));
    TEST_ASSERT_EQUAL_STRING("alice", user.name);
    TEST_ASSERT_EQUAL_STRING("1234", user.pin);
    TEST_ASSERT_EQUAL_UINT8(0x03, user.perm);
    TEST_ASSERT_TRUE((user.flags & DB_USER_FLAG_ENABLED) != 0);
    TEST_ASSERT_FALSE((user.flags & DB_USER_FLAG_ADMIN) != 0);

    memset(&update, 0, sizeof(update));
    strcpy(update.name, "bob");
    strcpy(update.pin, "5678");
    update.perm = 0x04;
    update.valid_from = 300;
    update.valid_until = 400;
    update.admin = true;
    TEST_ASSERT_EQUAL(0, service_user_manage_update_user(uid, &update));

    memset(&user, 0, sizeof(user));
    TEST_ASSERT_EQUAL(0, service_user_manage_get_user(uid, &user));
    TEST_ASSERT_EQUAL_UINT32(uid, user.uid);
    TEST_ASSERT_EQUAL_STRING("bob", user.name);
    TEST_ASSERT_EQUAL_STRING("5678", user.pin);
    TEST_ASSERT_EQUAL_UINT8(0x04, user.perm);
    TEST_ASSERT_EQUAL_UINT32(300, user.valid_from);
    TEST_ASSERT_EQUAL_UINT32(400, user.valid_until);
    TEST_ASSERT_TRUE((user.flags & DB_USER_FLAG_ENABLED) != 0);
    TEST_ASSERT_TRUE((user.flags & DB_USER_FLAG_ADMIN) != 0);

    memset(list, 0, sizeof(list));
    TEST_ASSERT_EQUAL(0, service_user_manage_list_users(list, 2, &total));
    TEST_ASSERT_EQUAL(1, total);
    TEST_ASSERT_EQUAL_UINT32(uid, list[0].uid);
}

void test_user_manage_check_permission_should_cover_basic_rules(void)
{
    service_user_create_req_t req;
    uint32_t uid = 0;
    bool allowed = true;
    db_user_record_t user;

    mock_reset();
    TEST_ASSERT_EQUAL(0, service_user_manage_init((service_face_detect_handle_t)0x1,
                                                  (service_face_identify_handle_t)0x2));

    memset(&req, 0, sizeof(req));
    strcpy(req.name, "perm");
    req.perm = 0x05;
    req.valid_from = 100;
    req.valid_until = 200;
    TEST_ASSERT_EQUAL(0, service_user_manage_create_user(&req, &uid));

    allowed = true;
    TEST_ASSERT_EQUAL(DB_ERR_NOT_FOUND,
                      service_user_manage_check_permission(99, 0, 150, &allowed));
    TEST_ASSERT_FALSE(allowed);

    TEST_ASSERT_EQUAL(0, service_db_user_get(uid, &user));
    user.flags = 0;
    TEST_ASSERT_EQUAL(0, service_db_user_update(uid, &user));
    allowed = true;
    TEST_ASSERT_EQUAL(0, service_user_manage_check_permission(uid, 0, 150, &allowed));
    TEST_ASSERT_FALSE(allowed);

    user.flags = DB_USER_FLAG_ENABLED;
    TEST_ASSERT_EQUAL(0, service_db_user_update(uid, &user));
    TEST_ASSERT_EQUAL(0, service_user_manage_check_permission(uid, 0, 99, &allowed));
    TEST_ASSERT_FALSE(allowed);
    TEST_ASSERT_EQUAL(0, service_user_manage_check_permission(uid, 0, 201, &allowed));
    TEST_ASSERT_FALSE(allowed);

    allowed = false;
    TEST_ASSERT_EQUAL(0, service_user_manage_check_permission(uid, 0, 150, &allowed));
    TEST_ASSERT_TRUE(allowed);

    allowed = true;
    TEST_ASSERT_EQUAL(0, service_user_manage_check_permission(uid, 0x02, 150, &allowed));
    TEST_ASSERT_FALSE(allowed);

    allowed = false;
    TEST_ASSERT_EQUAL(0, service_user_manage_check_permission(uid, 0x01, 150, &allowed));
    TEST_ASSERT_TRUE(allowed);

    user.valid_from = 0;
    user.valid_until = 0;
    TEST_ASSERT_EQUAL(0, service_db_user_update(uid, &user));
    allowed = false;
    TEST_ASSERT_EQUAL(0, service_user_manage_check_permission(uid, 0x05, 1, &allowed));
    TEST_ASSERT_TRUE(allowed);
}

void test_user_manage_delete_should_delete_face_before_user(void)
{
    service_user_create_req_t req;
    uint32_t uid = 0;

    mock_reset();
    TEST_ASSERT_EQUAL(0, service_user_manage_init((service_face_detect_handle_t)0x1,
                                                  (service_face_identify_handle_t)0x2));

    memset(&req, 0, sizeof(req));
    strcpy(req.name, "delete");
    TEST_ASSERT_EQUAL(0, service_user_manage_create_user(&req, &uid));
    TEST_ASSERT_EQUAL(0, service_user_manage_delete_user(uid));
    TEST_ASSERT_EQUAL_UINT32(uid, s_face_delete_uid);
    TEST_ASSERT_EQUAL_UINT32(uid, s_user_delete_uid);
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_user_manage_before_init_should_fail);
    RUN_TEST(test_user_manage_create_get_update_list_should_work);
    RUN_TEST(test_user_manage_check_permission_should_cover_basic_rules);
    RUN_TEST(test_user_manage_delete_should_delete_face_before_user);

    return UNITY_END();
}
