/**
 * @file    service_user_manage.h
 * @brief   用户管理服务 — 用户 CRUD、人脸录入与识别业务入口
 *
 * 封装 service_db、service_face_detect、service_face_identify，提供上层业务
 * 可直接调用的用户创建、人脸录入和单帧识别接口。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_USER_MANAGE_H
#define SERVICE_USER_MANAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "dal_camera.h"
#include "service_db.h"
#include "service_face_detect.h"
#include "service_face_identify.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     name[33];       /**< 用户名 */
    uint8_t  perm;           /**< 权限位 */
    char     pin[13];        /**< PIN */
    uint32_t valid_from;     /**< 有效期起始 */
    uint32_t valid_until;    /**< 有效期结束 */
    bool     admin;          /**< 是否管理员 */
} service_user_create_req_t;

typedef struct {
    uint32_t uid;            /**< 用户 ID */
    bool     matched;        /**< 是否识别通过 */
    float    similarity;     /**< 相似度 */
    uint8_t  score;          /**< 0~100 评分 */
} service_user_identify_result_t;

int service_user_manage_init(service_face_detect_handle_t detect,
                             service_face_identify_handle_t identify);

int service_user_manage_create_user(const service_user_create_req_t *req,
                                    uint32_t *out_uid);

int service_user_manage_enroll_face(uint32_t uid,
                                    const dal_camera_frame_t *frame);

int service_user_manage_identify_frame(const dal_camera_frame_t *frame,
                                       service_user_identify_result_t *out_result);

/**
 * @brief 检查用户是否具备本地通行权限
 *
 * 仅做用户存在、启用状态、有效期和权限位检查，不控制继电器。
 * required_perm 为 0 时表示不要求具体权限位。
 *
 * @param uid           用户 ID
 * @param required_perm 所需权限位掩码，0 表示不检查权限位
 * @param now           当前 Unix 时间戳
 * @param allowed       输出是否允许
 * @return 0 成功，负数错误码
 */
int service_user_manage_check_permission(uint32_t uid,
                                         uint8_t required_perm,
                                         uint32_t now,
                                         bool *allowed);

/**
 * @brief 更新用户基础信息
 *
 * 保留原 uid 和 registered 字段，不修改人脸特征。
 *
 * @param uid 用户 ID
 * @param req 新用户基础信息
 * @return 0 成功，负数错误码
 */
int service_user_manage_update_user(uint32_t uid,
                                    const service_user_create_req_t *req);

/**
 * @brief 查询用户信息
 * @param uid  用户 ID
 * @param user 输出用户记录
 * @return 0 成功，负数错误码
 */
int service_user_manage_get_user(uint32_t uid, db_user_record_t *user);

/**
 * @brief 列出有效用户
 * @param list  输出数组
 * @param max   数组最大容量
 * @param total 输出实际总数，可为 NULL
 * @return 0 成功，负数错误码
 */
int service_user_manage_list_users(db_user_record_t *list, int max, int *total);

int service_user_manage_delete_user(uint32_t uid);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_USER_MANAGE_H */
