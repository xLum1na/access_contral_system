/**
 * @file    service_db_user.c
 * @brief   用户管理模块 — JSON 行存储 + RAM 索引
 *
 * 持久化格式：一行一个 JSON，启动时全量加载。
 * 修改操作采用全量原子替换 (.tmp → rename)
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

#define TAG "DB_USER"

/* ================================================================
 *  内部函数声明
 * ================================================================ */

extern int  db_read_all(const char *sub_path, uint8_t **out_data, size_t *out_len);
extern int  db_write_atomic(const char *sub_path, const uint8_t *data, size_t len);
extern bool db_file_exists(const char *sub_path);

/* ---- 索引操作 ---- */
extern int      db_user_index_add(uint32_t uid, const user_record_t *record);
extern int      db_user_index_get(uint32_t uid, user_record_t *out);
extern int      db_user_index_update(uint32_t uid, const user_record_t *record);
extern int      db_user_index_delete(uint32_t uid);
extern int      db_user_index_find_by_name(const char *name, user_record_t *out);
extern int      db_user_index_list(user_record_t *list, int max, int *out_total);
extern int      db_user_index_count(void);
extern uint32_t db_user_index_next_uid(void);

/* ================================================================
 *  JSON 行编码 / 解码
 * ================================================================ */

/**
 * @brief 将 user_record_t 编码为 JSON 行
 *
 * 格式: {"uid":1,"name":"张三","perm":7,"pin":"123456","vf":0,"vu":0,"reg":1712567890,"upd":1712567890,"flg":0}
 * 字段名缩写以减少文件体积。
 *
 * @return 动态分配的字符串，调用者负责 free
 */
static char *user_to_json(const user_record_t *u)
{
    if (!u) return NULL;

    /* 预估长度：字段值 + JSON 结构 */
    char *buf = malloc(512);
    if (!buf) return NULL;

    int written = snprintf(buf, 512,
        "{\"uid\":%lu,\"nm\":\"%s\",\"pm\":%u,\"pn\":\"%s\","
        "\"vf\":%lu,\"vu\":%lu,\"reg\":%lu,\"upd\":%lu,\"fl\":%u}\n",
        (unsigned long)u->uid,
        u->name,
        (unsigned)u->perm,
        u->pin,
        (unsigned long)u->valid_from,
        (unsigned long)u->valid_until,
        (unsigned long)u->registered,
        (unsigned long)u->updated,
        (unsigned)u->flags);

    if (written < 0 || written >= 512) {
        free(buf);
        return NULL;
    }
    return buf;
}

/**
 * @brief 从 JSON 行解析 user_record_t
 *
 * 容错：解析失败返回 DB_ERR_FORMAT，单个字段缺失使用默认值。
 */
static int json_to_user(const char *line, user_record_t *u)
{
    if (!line || !u) return DB_ERR_PARAM;

    memset(u, 0, sizeof(*u));

    /* 简易 JSON 解析 — 仅支持本模块生成格式 */
    /* 提取各字段值 */
    const char *p = line;

    /* uid */
    p = strstr(p, "\"uid\":");
    if (!p) return DB_ERR_FORMAT;
    u->uid = (uint32_t)strtoul(p + 6, NULL, 10);

    /* nm (name) */
    p = strstr(line, "\"nm\":\"");
    if (p) {
        p += 6;
        const char *end = strchr(p, '\"');
        if (end) {
            size_t len = (size_t)(end - p);
            if (len > DB_USER_NAME_MAX_LEN) len = DB_USER_NAME_MAX_LEN;
            memcpy(u->name, p, len);
            u->name[len] = '\0';
        }
    }

    /* pm (perm) */
    p = strstr(line, "\"pm\":");
    if (p) u->perm = (uint8_t)strtoul(p + 5, NULL, 10);

    /* pn (pin) */
    p = strstr(line, "\"pn\":\"");
    if (p) {
        p += 6;
        const char *end = strchr(p, '\"');
        if (end) {
            size_t len = (size_t)(end - p);
            if (len > DB_USER_PIN_MAX_LEN) len = DB_USER_PIN_MAX_LEN;
            memcpy(u->pin, p, len);
            u->pin[len] = '\0';
        }
    }

    /* vf (valid_from) */
    p = strstr(line, "\"vf\":");
    if (p) u->valid_from = (uint32_t)strtoul(p + 5, NULL, 10);

    /* vu (valid_until) */
    p = strstr(line, "\"vu\":");
    if (p) u->valid_until = (uint32_t)strtoul(p + 5, NULL, 10);

    /* reg (registered) */
    p = strstr(line, "\"reg\":");
    if (p) u->registered = (uint32_t)strtoul(p + 6, NULL, 10);

    /* upd (updated) */
    p = strstr(line, "\"upd\":");
    if (p) u->updated = (uint32_t)strtoul(p + 6, NULL, 10);

    /* fl (flags) */
    p = strstr(line, "\"fl\":");
    if (p) u->flags = (uint8_t)strtoul(p + 5, NULL, 10);

    return DB_OK;
}

/* ================================================================
 *  SD 卡加载 / 同步
 * ================================================================ */

int db_user_load_from_sd(void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    int ret;

    ret = db_read_all(DB_USER_FILE, &data, &len);
    if (ret == DB_ERR_NOT_FOUND) {
        PAL_LOGI(TAG, "user.jsonl 不存在, 从空库启动");
        return DB_OK;
    }
    if (ret != DB_OK || !data) {
        PAL_LOGE(TAG, "读取 user.jsonl 失败: %d", ret);
        return ret;
    }

    /* 逐行解析 */
    char *saveptr = NULL;
    char *line = strtok_r((char *)data, "\n", &saveptr);
    int loaded = 0;
    uint32_t max_uid = 0;

    while (line) {
        /* 跳过空白行 */
        if (line[0] == '\0') { line = strtok_r(NULL, "\n", &saveptr); continue; }

        user_record_t u;
        if (json_to_user(line, &u) == DB_OK && u.uid > 0) {
            if (db_user_index_add(u.uid, &u) == DB_OK) {
                loaded++;
                if (u.uid > max_uid) max_uid = u.uid;
            }
        } else {
            PAL_LOGW(TAG, "跳过无效行: %.40s...", line);
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(data);
    PAL_LOGI(TAG, "加载 %d 条用户记录", loaded);
    return DB_OK;
}

int db_user_sync_to_sd(void)
{
    user_record_t records[DB_USER_MAX_COUNT];
    int total = 0;
    int ret;

    ret = db_user_index_list(records, DB_USER_MAX_COUNT, &total);
    if (ret != DB_OK) return ret;

    /* 构建完整 JSON 行文件内容 */
    /* 先计算总大小 */
    size_t total_size = 0;
    char **lines = calloc((size_t)total, sizeof(char *));
    if (!lines) return DB_ERR_NO_MEM;

    int valid_count = 0;
    for (int i = 0; i < total; i++) {
        if (records[i].flags & DB_USER_FLAG_DELETED) continue;
        lines[valid_count] = user_to_json(&records[i]);
        if (lines[valid_count]) {
            total_size += strlen(lines[valid_count]);
            valid_count++;
        }
    }

    /* 拼接为单个 buffer */
    uint8_t *buf = malloc(total_size);
    if (!buf && total_size > 0) {
        for (int i = 0; i < valid_count; i++) free(lines[i]);
        free(lines);
        return DB_ERR_NO_MEM;
    }

    size_t pos = 0;
    for (int i = 0; i < valid_count; i++) {
        size_t l = strlen(lines[i]);
        memcpy(buf + pos, lines[i], l);
        pos += l;
        free(lines[i]);
    }
    free(lines);

    /* 原子写入 */
    ret = db_write_atomic(DB_USER_FILE, buf, pos);
    free(buf);

    if (ret == DB_OK) {
        PAL_LOGI(TAG, "同步 %d 条用户记录到 SD", valid_count);
    }
    return ret;
}

/* ================================================================
 *  API 实现
 * ================================================================ */

int service_db_user_add(const user_record_t *user, uint32_t *out_uid)
{
    if (!user || !out_uid) return DB_ERR_PARAM;

    uint32_t uid = db_user_index_next_uid();
    if (uid == 0) return DB_ERR_NOT_INIT;

    user_record_t u = *user;
    u.uid        = uid;
    u.registered = (uint32_t)time(NULL);
    u.updated    = u.registered;
    u.flags     |= DB_USER_FLAG_ENABLED;

    int ret = db_user_index_add(uid, &u);
    if (ret != DB_OK) return ret;

    /* 同步到 SD */
    db_user_sync_to_sd();

    *out_uid = uid;
    PAL_LOGI(TAG, "添加用户: uid=%lu name=%s", (unsigned long)uid, u.name);
    return DB_OK;
}

int service_db_user_get(uint32_t uid, user_record_t *user)
{
    if (uid == 0 || !user) return DB_ERR_PARAM;
    return db_user_index_get(uid, user);
}

int service_db_user_update(uint32_t uid, const user_record_t *user)
{
    if (uid == 0 || !user) return DB_ERR_PARAM;

    user_record_t u = *user;
    u.uid     = uid;
    u.updated = (uint32_t)time(NULL);

    int ret = db_user_index_update(uid, &u);
    if (ret != DB_OK) return ret;

    db_user_sync_to_sd();
    PAL_LOGI(TAG, "更新用户: uid=%lu", (unsigned long)uid);
    return DB_OK;
}

int service_db_user_delete(uint32_t uid)
{
    if (uid == 0) return DB_ERR_PARAM;

    int ret = db_user_index_delete(uid);
    if (ret != DB_OK) return ret;

    db_user_sync_to_sd();
    PAL_LOGI(TAG, "删除用户(软): uid=%lu", (unsigned long)uid);
    return DB_OK;
}

int service_db_user_find_by_name(const char *name, user_record_t *user)
{
    if (!name || !user) return DB_ERR_PARAM;
    return db_user_index_find_by_name(name, user);
}

int service_db_user_list(user_record_t *list, int max, int *total)
{
    if (!list || max <= 0) return DB_ERR_PARAM;
    return db_user_index_list(list, max, total);
}

int service_db_user_count(void)
{
    return db_user_index_count();
}
