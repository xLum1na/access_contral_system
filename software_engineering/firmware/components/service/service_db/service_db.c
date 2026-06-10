/**
 * @file    service_db.c
 * @brief   本地数据库 — 初始化 / 反初始化 / 统计
 *
 * 数据库生命周期入口，负责：
 * - 创建目录结构
 * - 恢复未完成的 journal
 * - 从 SD 卡加载索引到 RAM
 * - 提供统计信息
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#include "service_db_internal.h"
#include "pal_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#define TAG "DB"

/* ================================================================
 *  全局状态
 * ================================================================ */

/** @brief SD 卡挂载点基路径，由 init 设置 */
char g_db_base_path[DB_MAX_PATH] = {0};

static bool g_inited = false;

/* ---- 内部子模块初始化声明 ---- */
extern int  db_user_index_init(void);
extern void db_user_index_deinit(void);
extern int  db_face_index_init(void);
extern void db_face_index_deinit(void);
extern int  db_face_index_count(void);
extern int  db_user_index_count(void);

/* ---- 文件 I/O 声明 ---- */
extern int  db_ensure_dirs(void);
extern int  db_recover_journal(void);
extern int  db_read_all(const char *sub_path, uint8_t **out_data, size_t *out_len);
extern int  db_write_sync(const char *sub_path, const uint8_t *data, size_t len, bool append);
extern int  db_write_atomic(const char *sub_path, const uint8_t *data, size_t len);
extern int  db_unlink(const char *sub_path);
extern bool db_file_exists(const char *sub_path);
extern long db_file_size(const char *sub_path);

/* ---- 用户持久化加载声明 ---- */
extern int db_user_load_from_sd(void);
extern int db_user_sync_to_sd(void);

/* ---- 特征持久化加载声明 ---- */
extern int db_face_load_idx_from_sd(void);

/* ================================================================
 *  初始化 / 反初始化
 * ================================================================ */

int service_db_init(const char *mount_point)
{
    if (g_inited) return DB_OK;
    if (!mount_point || mount_point[0] == '\0') return DB_ERR_PARAM;

    /* 保存基路径 */
    size_t mlen = strlen(mount_point);
    if (mlen >= sizeof(g_db_base_path) - 32) return DB_ERR_PARAM; /* 留空间给子路径 */
    strcpy(g_db_base_path, mount_point);

    PAL_LOGI(TAG, "初始化数据库: %s", g_db_base_path);

    /* 1. 创建目录结构 */
    if (db_ensure_dirs() != DB_OK) {
        PAL_LOGE(TAG, "创建目录结构失败");
        return DB_ERR_IO;
    }

    /* 2. 恢复未完成的 journal */
    if (db_recover_journal() != DB_OK) {
        PAL_LOGW(TAG, "journal 恢复失败，继续初始化");
    }

    /* 3. 初始化 RAM 索引 */
    if (db_user_index_init() != DB_OK) {
        PAL_LOGE(TAG, "用户索引初始化失败");
        return DB_ERR_NO_MEM;
    }
    if (db_face_index_init() != DB_OK) {
        PAL_LOGE(TAG, "特征索引初始化失败");
        db_user_index_deinit();
        return DB_ERR_NO_MEM;
    }

    /* 4. 从 SD 卡加载用户数据 */
    if (db_user_load_from_sd() != DB_OK) {
        PAL_LOGW(TAG, "加载用户数据失败，从空库启动");
    }

    /* 5. 从 SD 卡加载特征索引 */
    if (db_face_load_idx_from_sd() != DB_OK) {
        PAL_LOGW(TAG, "加载特征索引失败，从空库启动");
    }

    g_inited = true;
    PAL_LOGI(TAG, "数据库初始化完成 (用户:%d, 特征:%d)",
             db_user_index_count(), db_face_index_count());
    return DB_OK;
}

int service_db_deinit(void)
{
    if (!g_inited) return DB_ERR_NOT_INIT;

    PAL_LOGI(TAG, "关闭数据库...");

    /* 同步内存数据到 SD */
    db_user_sync_to_sd();

    /* 释放 RAM 索引 */
    db_face_index_deinit();
    db_user_index_deinit();

    g_inited = false;
    g_db_base_path[0] = '\0';

    PAL_LOGI(TAG, "数据库已关闭");
    return DB_OK;
}

/* ================================================================
 *  统计信息
 * ================================================================ */

int service_db_get_stats(db_stats_t *stats)
{
    if (!g_inited || !stats) return DB_ERR_NOT_INIT;

    memset(stats, 0, sizeof(*stats));

    stats->user_max    = DB_USER_MAX_COUNT;
    stats->user_count  = db_user_index_count();
    stats->face_count  = db_face_index_count();

    /* 特征总大小 */
    long fsize = db_file_size(DB_FACE_DAT);
    if (fsize > 0) stats->face_total_bytes = (uint32_t)fsize;

    /* 日志总条数 — 汇总所有月份 */
    extern int db_log_get_total_entries(void);
    stats->log_total_entries = db_log_get_total_entries();

    /* SD 卡容量信息通过 dal_storage_api 查询（不在此层直接调用） */
    stats->sd_total_mb = 0;
    stats->sd_free_mb  = 0;

    return DB_OK;
}
