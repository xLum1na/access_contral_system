/**
 * @file    service_user_manage.c
 * @brief   用户管理服务 — 用户 CRUD、人脸录入与识别业务入口
 */

#include "service_user_manage.h"

#include "pal_log.h"

#include <string.h>
#include <time.h>

#define TAG "USER_MANAGE"

static service_face_detect_handle_t   s_detect_handle = NULL;
static service_face_identify_handle_t s_identify_handle = NULL;
static bool s_inited = false;

int service_user_manage_init(service_face_detect_handle_t detect,
                             service_face_identify_handle_t identify)
{
    s_detect_handle = detect;
    s_identify_handle = identify;
    s_inited = true;
    PAL_LOGI(TAG, "用户管理服务初始化完成");
    return 0;
}

int service_user_manage_create_user(const service_user_create_req_t *req,
                                    uint32_t *out_uid)
{
    if (!s_inited || !req || !out_uid || req->name[0] == '\0') return -1;

    db_user_record_t user;
    memset(&user, 0, sizeof(user));
    strncpy(user.name, req->name, sizeof(user.name) - 1);
    strncpy(user.pin, req->pin, sizeof(user.pin) - 1);
    user.perm = req->perm;
    user.valid_from = req->valid_from;
    user.valid_until = req->valid_until;
    user.registered = (uint32_t)time(NULL);
    user.updated = user.registered;
    user.flags = DB_USER_FLAG_ENABLED | (req->admin ? DB_USER_FLAG_ADMIN : 0);

    return service_db_user_add(&user, out_uid);
}

int service_user_manage_enroll_face(uint32_t uid,
                                    const dal_camera_frame_t *frame)
{
    if (!s_inited || !s_detect_handle || !s_identify_handle || !frame || uid == 0) return -1;

    service_face_box_t boxes[2];
    int face_count = 0;
    int ret = service_face_detect_run(s_detect_handle, frame, boxes, 2, &face_count);
    if (ret != 0 || face_count != 1) {
        PAL_LOGW(TAG, "人脸录入失败：检测结果 ret=%d count=%d", ret, face_count);
        return -1;
    }

    uint8_t feature[256];
    uint32_t feature_size = 0;
    ret = service_face_identify_extract(s_identify_handle, frame, &boxes[0],
                                        feature, sizeof(feature), &feature_size);
    if (ret != 0 || feature_size == 0) {
        PAL_LOGW(TAG, "人脸录入失败：特征提取 ret=%d size=%lu",
                 ret, (unsigned long)feature_size);
        return -1;
    }

    service_face_identify_status_t status;
    memset(&status, 0, sizeof(status));
    (void)service_face_identify_get_status(s_identify_handle, &status);

    return service_db_face_set(uid, feature, feature_size, status.model_id);
}

int service_user_manage_identify_frame(const dal_camera_frame_t *frame,
                                       service_user_identify_result_t *out_result)
{
    if (!s_inited || !s_detect_handle || !s_identify_handle || !frame || !out_result) return -1;

    memset(out_result, 0, sizeof(*out_result));

    service_face_box_t boxes[2];
    int face_count = 0;
    int ret = service_face_detect_run(s_detect_handle, frame, boxes, 2, &face_count);
    if (ret != 0 || face_count <= 0) {
        PAL_LOGW(TAG, "识别失败：检测 ret=%d count=%d", ret, face_count);
        return ret ? ret : -1;
    }

    service_face_match_t match;
    memset(&match, 0, sizeof(match));
    ret = service_face_identify_identify(s_identify_handle, frame, &boxes[0], &match);
    if (ret != 0) {
        PAL_LOGW(TAG, "识别失败：特征匹配 ret=%d", ret);
        return ret;
    }

    out_result->uid = match.uid;
    out_result->matched = match.matched;
    out_result->similarity = match.similarity;
    out_result->score = match.score;
    return 0;
}

int service_user_manage_check_permission(uint32_t uid,
                                         uint8_t required_perm,
                                         uint32_t now,
                                         bool *allowed)
{
    db_user_record_t user;
    int ret;

    if (allowed) {
        *allowed = false;
    }
    if (!s_inited || uid == 0 || !allowed) return -1;

    memset(&user, 0, sizeof(user));
    ret = service_db_user_get(uid, &user);
    if (ret != 0) return ret;

    if ((user.flags & DB_USER_FLAG_ENABLED) == 0) return 0;
    if (user.valid_from != 0 && now < user.valid_from) return 0;
    if (user.valid_until != 0 && now > user.valid_until) return 0;
    if (required_perm != 0 && (user.perm & required_perm) != required_perm) return 0;

    *allowed = true;
    return 0;
}

int service_user_manage_update_user(uint32_t uid,
                                    const service_user_create_req_t *req)
{
    db_user_record_t user;
    uint32_t registered;
    int ret;

    if (!s_inited || uid == 0 || !req || req->name[0] == '\0') return -1;

    memset(&user, 0, sizeof(user));
    ret = service_db_user_get(uid, &user);
    if (ret != 0) return ret;

    registered = user.registered;
    memset(&user, 0, sizeof(user));
    user.uid = uid;
    strncpy(user.name, req->name, sizeof(user.name) - 1);
    strncpy(user.pin, req->pin, sizeof(user.pin) - 1);
    user.perm = req->perm;
    user.valid_from = req->valid_from;
    user.valid_until = req->valid_until;
    user.registered = registered;
    user.updated = (uint32_t)time(NULL);
    user.flags = DB_USER_FLAG_ENABLED | (req->admin ? DB_USER_FLAG_ADMIN : 0);

    return service_db_user_update(uid, &user);
}

int service_user_manage_get_user(uint32_t uid, db_user_record_t *user)
{
    if (!s_inited || uid == 0 || !user) return -1;
    return service_db_user_get(uid, user);
}

int service_user_manage_list_users(db_user_record_t *list, int max, int *total)
{
    if (!s_inited || !list || max <= 0) return -1;
    return service_db_user_list(list, max, total);
}

int service_user_manage_delete_user(uint32_t uid)
{
    if (!s_inited || uid == 0) return -1;

    (void)service_db_face_delete(uid);
    return service_db_user_delete(uid);
}
