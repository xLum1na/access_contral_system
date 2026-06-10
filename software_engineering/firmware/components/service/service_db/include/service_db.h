/**
 * @file    service_db.h
 * @brief   本地数据库 — 公开 API
 *
 * 基于 SD 卡的人脸识别设备本地数据库，支持：
 * - 用户元数据管理 (JSON 行存储)
 * - 人脸特征向量存储 (定长索引 + 变长数据，含断电保护 journal)
 * - 通行记录日志 (按月分文件，定长二进制追加)
 *
 * 使用前需先挂载 SD 卡 (dal_storage_init)，然后调用 service_db_init()。
 *
 * 参考文档：项目计划 glistening-crafting-blossom
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_DB_H
#define SERVICE_DB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  类型定义
 * ================================================================ */

/** @brief 用户标志位 */
#define DB_USER_FLAG_ENABLED    0x01  /**< 启用 */
#define DB_USER_FLAG_ADMIN      0x02  /**< 管理员 */

/** @brief 用户记录 */
typedef struct {
    uint32_t uid;              /**< 用户 ID（自增主键，>0） */
    char     name[33];         /**< UTF-8 姓名（最长 32 字节） */
    uint8_t  perm;             /**< 权限位掩码 */
    char     pin[13];          /**< 数字密码（最长 12 位） */
    uint32_t valid_from;       /**< 有效期起始 (Unix 时间戳, 0=不限) */
    uint32_t valid_until;      /**< 有效期结束 (Unix 时间戳, 0=不限) */
    uint32_t registered;       /**< 注册时间戳 */
    uint32_t updated;          /**< 最后更新时间戳 */
    uint8_t  flags;            /**< 状态标志 (DB_USER_FLAG_*) */
} db_user_record_t;

/** @brief 事件类型 */
typedef enum {
    DB_LOG_PASS       = 0,  /**< 通行成功 */
    DB_LOG_DENY       = 1,  /**< 通行拒绝 */
    DB_LOG_DURESS     = 2,  /**< 胁迫报警 */
    DB_LOG_ADMIN      = 3,  /**< 管理员操作 */
    DB_LOG_SYSTEM     = 4,  /**< 系统事件 */
} db_log_event_t;

/** @brief 通行记录条目 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;       /**< Unix 时间戳 */
    uint32_t uid;             /**< 用户 ID (0=陌生人) */
    uint8_t  event_type;      /**< 事件类型 (db_log_event_t) */
    uint8_t  match_score;     /**< 匹配置信度 0-100 */
    uint8_t  reserved[2];     /**< 对齐保留 */
    float    similarity;      /**< 特征相似度 (调试用) */
    uint32_t image_id;        /**< 关联抓拍图片 ID（0=无） */
    uint32_t checksum;        /**< 本条 CRC32 */
} db_log_entry_t;

/** @brief 数据库统计信息 */
typedef struct {
    uint32_t user_count;       /**< 当前用户数 */
    uint32_t user_max;         /**< 最大用户数 */
    uint32_t face_count;       /**< 已录入特征数 */
    uint32_t face_total_bytes; /**< 特征数据总大小 */
    uint32_t log_total_entries;/**< 日志总条数 */
    uint32_t sd_total_mb;      /**< SD 卡总容量 MB */
    uint32_t sd_free_mb;       /**< SD 卡剩余 MB */
} db_stats_t;

/**
 * @brief 特征遍历回调
 * @param uid           用户 ID
 * @param feature       特征数据指针
 * @param feature_size  特征字节数
 * @param arg           用户自定义参数
 * @return 0 继续遍历，非 0 停止
 */
typedef int (*db_face_iterate_cb_t)(uint32_t uid, const uint8_t *feature,
                                    uint32_t feature_size, void *arg);

/* ================================================================
 *  错误码
 * ================================================================ */

typedef enum {
    DB_OK                =  0,  /**< 成功 */
    DB_ERR_PARAM         = -1,  /**< 参数无效 */
    DB_ERR_NOT_INIT      = -2,  /**< 未初始化 */
    DB_ERR_IO            = -3,  /**< 文件 I/O 错误 */
    DB_ERR_NO_MEM        = -4,  /**< 内存不足 */
    DB_ERR_FULL          = -5,  /**< 容量已满 */
    DB_ERR_NOT_FOUND     = -6,  /**< 记录不存在 */
    DB_ERR_DUPLICATE     = -7,  /**< 重复记录 */
    DB_ERR_CHECKSUM      = -8,  /**< 校验失败 */
} db_err_t;

/* ================================================================
 *  生命周期
 * ================================================================ */

/**
 * @brief 初始化数据库（挂载 SD 卡后调用）
 *
 * 自动创建目录结构、恢复未完成的 journal、
 * 从 SD 卡加载用户和特征索引到 RAM。
 *
 * @param mount_point SD 卡挂载点路径（如 "/sdcard"）
 * @return 0 成功，负数错误码
 *
 * @note 可重复调用（已初始化直接返回 OK）
 */
int service_db_init(const char *mount_point);

/**
 * @brief 关闭数据库，释放 RAM 资源
 *
 * 关闭前自动将内存数据同步到 SD 卡。
 *
 * @return 0 成功
 */
int service_db_deinit(void);

/* ================================================================
 *  用户管理
 * ================================================================ */

/**
 * @brief 添加用户
 * @param user   用户信息（uid 字段自动分配）
 * @param out_uid 输出分配的 UID
 * @return 0 成功
 */
int service_db_user_add(const db_user_record_t *user, uint32_t *out_uid);

/**
 * @brief 查询用户
 * @param uid  用户 ID
 * @param user 输出用户信息
 * @return 0 成功，DB_ERR_NOT_FOUND 不存在
 */
int service_db_user_get(uint32_t uid, db_user_record_t *user);

/**
 * @brief 更新用户信息
 * @param uid  用户 ID
 * @param user 新信息
 * @return 0 成功
 */
int service_db_user_update(uint32_t uid, const db_user_record_t *user);

/**
 * @brief 删除用户（软删除，保留数据可恢复）
 * @param uid 用户 ID
 * @return 0 成功
 */
int service_db_user_delete(uint32_t uid);

/**
 * @brief 按姓名查找用户
 * @param name 姓名（精确匹配）
 * @param user 输出用户信息
 * @return 0 成功，DB_ERR_NOT_FOUND 未找到
 */
int service_db_user_find_by_name(const char *name, db_user_record_t *user);

/**
 * @brief 列出所有有效用户
 * @param list  输出数组
 * @param max   数组最大容量
 * @param total 输出实际总数（可选，传 NULL 忽略）
 * @return 0 成功
 */
int service_db_user_list(db_user_record_t *list, int max, int *total);

/** @brief 获取当前用户数 */
int service_db_user_count(void);

/* ================================================================
 *  人脸特征管理
 * ================================================================ */

/**
 * @brief 录入 / 更新人脸特征
 *
 * 自动检测是否已存在（同 uid 则更新），
 * 写入过程通过 journal 保证断电安全。
 *
 * @param uid        用户 ID
 * @param feature    特征数据
 * @param size       特征字节数
 * @param model_id   模型标识（防止不同模型特征混用）
 * @return 0 成功
 */
int service_db_face_set(uint32_t uid, const uint8_t *feature,
                        uint32_t size, uint32_t model_id);

/**
 * @brief 查询人脸特征
 *
 * 优先从 RAM 获取，若未加载则从 SD 卡读取。
 *
 * @param uid     用户 ID
 * @param feature 输出缓冲区
 * @param size    输入时=缓冲区大小，输出时=实际大小
 * @return 0 成功，DB_ERR_NO_MEM 缓冲区不足
 */
int service_db_face_get(uint32_t uid, uint8_t *feature, uint32_t *size);

/**
 * @brief 删除人脸特征（软删除）
 * @param uid 用户 ID
 * @return 0 成功
 */
int service_db_face_delete(uint32_t uid);

/**
 * @brief 加载全部特征到 RAM（供 1:N 人脸比对使用）
 *
 * 加载后可调用 service_db_face_iterate() 遍历。
 * 大量特征时（>500）自动分片加载。
 *
 * @return 0 成功
 */
int service_db_face_load_all(void);

/**
 * @brief 释放特征占用的 RAM（特征索引保留）
 */
void service_db_face_unload(void);

/**
 * @brief 遍历已加载的特征（供 1:N 比对）
 * @param cb  回调函数，返回非 0 停止遍历
 * @param arg 用户自定义参数
 * @return 0 全部遍历，>0 被回调中断
 *
 * @note 若特征未加载到 RAM，自动调用 service_db_face_load_all()
 */
int service_db_face_iterate(db_face_iterate_cb_t cb, void *arg);

/** @brief 获取已录入特征数 */
int service_db_face_count(void);

/* ================================================================
 *  通行记录
 * ================================================================ */

/**
 * @brief 追加一条通行记录
 *
 * 自动追加到当月 .bin 文件，带 CRC32 校验。
 *
 * @param uid         用户 ID (0=陌生人)
 * @param event_type  事件类型
 * @param match_score 匹配置信度 (0-100)
 * @param similarity  相似度值 (调试用)
 * @param image_id    关联图片 ID (0=无)
 * @return 0 成功
 */
int service_db_log_append(uint32_t uid, uint8_t event_type,
                          uint8_t match_score, float similarity, uint32_t image_id);

/**
 * @brief 按条件查询通行记录
 *
 * @param start_time 起始时间戳 (Unix)
 * @param end_time   结束时间戳 (0=当前时间+1天)
 * @param uid        用户 ID 过滤 (0=不过滤)
 * @param list       输出数组
 * @param max        数组最大容量
 * @param total      输出符合条件的总数（可选）
 * @return 0 成功
 */
int service_db_log_query(uint32_t start_time, uint32_t end_time,
                         uint32_t uid, db_log_entry_t *list, int max, int *total);

/**
 * @brief 获取最近 N 条记录
 * @param list  输出数组
 * @param count 期望条数
 * @return 实际返回条数（可能小于 count）
 */
int service_db_log_get_recent(db_log_entry_t *list, int count);

/**
 * @brief 清理指定时间之前的日志
 * @param before_time 此时间之前的日志将被删除
 * @return 删除的文件数
 */
int service_db_log_cleanup(uint32_t before_time);

/* ================================================================
 *  统计信息
 * ================================================================ */

/**
 * @brief 获取数据库统计信息
 * @param stats 输出统计
 * @return 0 成功
 */
int service_db_get_stats(db_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_DB_H */
