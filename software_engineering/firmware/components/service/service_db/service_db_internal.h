/**
 * @file    service_db_internal.h
 * @brief   本地数据库 — 内部共享结构体、常量、工具宏
 *
 * 本文件仅供 service 组件内部使用，不对外暴露。
 *
 * 参考文档：计划 glistening-crafting-blossom
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_DB_INTERNAL_H
#define SERVICE_DB_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  路径常量
 * ================================================================ */

#define DB_DIR              "/db"
#define DB_USER_FILE        "/db/user.jsonl"
#define DB_USER_TMP         "/db/user.jsonl.tmp"
#define DB_FACE_IDX         "/db/face.idx"
#define DB_FACE_DAT         "/db/face.dat"
#define DB_FACE_JOURNAL     "/db/face.dat.journal"
#define DB_LOG_DIR          "/db/access_log"
#define DB_LOG_INDEX        "/db/access_log/index.idx"
#define DB_BAK_DIR          "/db/bak"
#define DB_LOCK_FILE        "/db/db.lock"

/* 外部传入挂载点，内部拼接路径 */
#define DB_MAX_PATH         128

/* ================================================================
 *  容量限制
 * ================================================================ */

#define DB_USER_MAX_COUNT       2000    /**< 最大用户数 */
#define DB_USER_NAME_MAX_LEN    32      /**< 用户名字节数 (不含 \\0) */
#define DB_USER_PIN_MAX_LEN     12      /**< 密码最大位数 */
#define DB_FACE_IDX_ENTRY_SIZE  64      /**< 每条索引定长 64B */
#define DB_FACE_SHARD_COUNT     4       /**< 特征分片数 (2000人/4=500人/片) */
#define DB_FACE_SHARD_SIZE      (DB_USER_MAX_COUNT / DB_FACE_SHARD_COUNT) /**< 500 */
#define DB_LOG_ENTRY_SIZE       24      /**< 每条日志 24B */

/* ================================================================
 *  魔数 / 校验常量
 * ================================================================ */

#define DB_FACE_MAGIC           0xFACEFEEDU  /**< face.dat 记录尾魔数 */
#define DB_LOG_MAGIC            0xACCE5500U  /**< 日志条目尾魔数 */

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
    DB_ERR_JOURNAL       = -9,  /**< journal 损坏 */
    DB_ERR_FORMAT        = -10, /**< 数据格式错误 */
} service_db_err_t;

/* ================================================================
 *  用户元数据
 * ================================================================ */

/** @brief 用户标志位 */
#define DB_USER_FLAG_ENABLED    0x01  /**< 启用 */
#define DB_USER_FLAG_ADMIN      0x02  /**< 管理员 */
#define DB_USER_FLAG_DELETED    0x80  /**< 已软删除 */

typedef struct {
    uint32_t uid;              /**< 用户 ID（自增主键，>0） */
    char     name[DB_USER_NAME_MAX_LEN + 1]; /**< UTF-8 姓名 */
    uint8_t  perm;             /**< 权限位掩码 */
    char     pin[DB_USER_PIN_MAX_LEN + 1];   /**< 数字密码 */
    uint32_t valid_from;       /**< 有效期起始 (Unix 时间戳, 0=不限) */
    uint32_t valid_until;      /**< 有效期结束 (Unix 时间戳, 0=不限) */
    uint32_t registered;       /**< 注册时间戳 */
    uint32_t updated;          /**< 最后更新时间戳 */
    uint8_t  flags;            /**< 状态标志 (DB_USER_FLAG_*) */
} user_record_t;

/* ================================================================
 *  人脸特征索引条目 (face.idx 一条 = 64B)
 * ================================================================ */

/** @brief 特征索引标志位 */
#define DB_FACE_FLAG_ENABLED   0x01
#define DB_FACE_FLAG_DELETED   0x80

typedef struct __attribute__((packed)) {
    uint32_t uid;              /**< 用户 ID */
    uint32_t feature_size;     /**< 特征字节数 */
    uint32_t feature_offset;   /**< face.dat 中偏移 */
    uint32_t checksum;         /**< 特征数据 CRC32 */
    uint32_t created_at;       /**< 创建时间戳 */
    uint32_t updated_at;       /**< 更新时间戳 */
    uint32_t model_id;         /**< 模型标识 */
    uint8_t  name_hash[8];     /**< 用户名哈希 (SipHash-2-4 截断) */
    uint8_t  flags;            /**< 状态标志 */
    uint8_t  reserved[27];     /**< 预留，64B 对齐 */
} face_index_entry_t;

_Static_assert(sizeof(face_index_entry_t) == 64, "face_index_entry_t must be 64B");

/* ================================================================
 *  通行记录日志条目 (24B)
 * ================================================================ */

/** @brief 事件类型 */
typedef enum {
    DB_LOG_PASS       = 0,  /**< 通行成功 */
    DB_LOG_DENY       = 1,  /**< 通行拒绝 */
    DB_LOG_DURESS     = 2,  /**< 胁迫报警 */
    DB_LOG_ADMIN      = 3,  /**< 管理员操作 */
    DB_LOG_SYSTEM     = 4,  /**< 系统事件 */
} db_log_event_t;

typedef struct __attribute__((packed)) {
    uint32_t timestamp;       /**< Unix 时间戳 */
    uint32_t uid;             /**< 用户 ID (0=陌生人) */
    uint8_t  event_type;      /**< 事件类型 (db_log_event_t) */
    uint8_t  match_score;     /**< 匹配置信度 0-100 */
    uint8_t  reserved[2];     /**< 对齐保留 */
    float    similarity;      /**< 特征相似度 (调试用) */
    uint32_t image_id;        /**< 关联抓拍图片 ID（0=无） */
    uint32_t checksum;        /**< 本条 CRC32 */
} log_entry_t;

_Static_assert(sizeof(log_entry_t) == 24, "log_entry_t must be 24B");

/* ================================================================
 *  日志月索引条目
 * ================================================================ */

typedef struct {
    uint32_t year_month;      /**< YYYYMM 格式 */
    uint32_t record_count;    /**< 该月记录总数 */
    uint32_t first_time;      /**< 该月最早时间戳 */
    uint32_t last_time;       /**< 该月最晚时间戳 */
} log_index_entry_t;

/* ================================================================
 *  特征遍历回调
 * ================================================================ */

/**
 * @brief 特征遍历回调
 * @param uid           用户 ID
 * @param feature       特征数据指针 (PSRAM)
 * @param feature_size  特征字节数
 * @param arg           用户自定义参数
 * @return 0 继续遍历，非 0 停止
 */
typedef int (*face_iterate_cb_t)(uint32_t uid, const uint8_t *feature,
                                 uint32_t feature_size, void *arg);

/* ================================================================
 *  数据库统计信息
 * ================================================================ */

typedef struct {
    uint32_t user_count;       /**< 当前用户数 */
    uint32_t user_max;         /**< 最大用户数 */
    uint32_t face_count;       /**< 已录入特征数 */
    uint32_t face_total_bytes; /**< 特征数据总大小 */
    uint32_t log_total_entries;/**< 日志总条数 */
    uint32_t sd_total_mb;      /**< SD 卡总容量 MB */
    uint32_t sd_free_mb;       /**< SD 卡剩余 MB */
} db_stats_t;

/* ================================================================
 *  内部工具函数
 * ================================================================ */

/**
 * @brief 拼接数据库完整路径
 * @param buf       输出缓冲区
 * @param buf_size  缓冲区大小
 * @param sub_path  子路径 (如 "/db/user.jsonl")
 * @return 0 成功，负数为错误码
 */
int db_make_path(char *buf, size_t buf_size, const char *sub_path);

/**
 * @brief CRC32 计算 (IEEE 802.3 多项式)
 */
uint32_t db_crc32(const uint8_t *data, size_t len);

/**
 * @brief 简单字符串哈希 (djb2)
 */
uint64_t db_hash_string(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_DB_INTERNAL_H */
