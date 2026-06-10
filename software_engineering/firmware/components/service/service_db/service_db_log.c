/**
 * @file    service_db_log.c
 * @brief   通行记录管理 — 按月分文件 + 定长二进制追加
 *
 * 文件命名：access_log/YYYYMM.bin
 * 每条记录 24B 定长，末尾含 CRC32 校验。
 * 查询策略：根据时间范围确定文件列表 → 顺序扫描过滤。
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
#include <dirent.h>

#define TAG "DB_LOG"

/* ================================================================
 *  内部函数声明
 * ================================================================ */

extern char  g_db_base_path[DB_MAX_PATH];
extern int   db_make_path(char *buf, size_t buf_size, const char *sub_path);
extern int   db_write_sync(const char *sub_path, const uint8_t *data, size_t len, bool append);
extern bool  db_file_exists(const char *sub_path);
extern long  db_file_size(const char *sub_path);
extern int   db_unlink(const char *sub_path);
extern DIR  *db_opendir(const char *sub_path);
extern uint32_t db_crc32(const uint8_t *data, size_t len);

/* ================================================================
 *  日志月索引缓存 (RAM)
 * ================================================================ */

#define LOG_INDEX_MAX 64  /* 覆盖 5 年以上 */

typedef struct {
    uint32_t year_month;
    uint32_t record_count;
    uint32_t first_time;
    uint32_t last_time;
    char     filename[32];
} log_month_cache_t;

static log_month_cache_t g_log_cache[LOG_INDEX_MAX];
static int               g_log_cache_count = 0;
static uint32_t          g_log_total_entries = 0;

/* ================================================================
 *  文件名工具
 * ================================================================ */

/**
 * @brief 时间戳 → 月文件名 "access_log/YYYYMM.bin"
 */
static void make_log_name(uint32_t timestamp, char *name, size_t name_size)
{
    time_t t = (time_t)timestamp;
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(name, name_size, "%s/%04d%02d.bin",
             DB_LOG_DIR, tm.tm_year + 1900, tm.tm_mon + 1);
}

/**
 * @brief 从文件名解析年月 (YYYYMM.bin → YYYYMM)
 */
static uint32_t parse_year_month(const char *filename)
{
    const char *base = strrchr(filename, '/');
    if (!base) base = filename;
    else      base++;

    int y, m;
    if (sscanf(base, "%4d%2d.bin", &y, &m) == 2) {
        return (uint32_t)(y * 100 + m);
    }
    return 0;
}

/**
 * @brief 年月 → 时间戳范围 [first, last]（开区间 first, 闭区间 last）
 */
static void month_to_time_range(uint32_t year_month, uint32_t *first, uint32_t *last)
{
    int y = (int)(year_month / 100);
    int m = (int)(year_month % 100);

    struct tm tm = {0};
    tm.tm_year = y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = 1;
    tm.tm_hour = 0;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;
    *first = (uint32_t)mktime(&tm);

    /* 下月 1 号 - 1 秒 */
    tm.tm_mon++;
    if (tm.tm_mon > 11) { tm.tm_mon = 0; tm.tm_year++; }
    *last = (uint32_t)mktime(&tm) - 1;
}

/* ================================================================
 *  月缓存刷新
 * ================================================================ */

/**
 * @brief 扫描 access_log 目录，刷新月缓存
 */
static int log_refresh_cache(void)
{
    DIR *d = db_opendir(DB_LOG_DIR);
    if (!d) {
        g_log_cache_count = 0;
        g_log_total_entries = 0;
        return DB_OK; /* 目录不存在，正常 */
    }

    g_log_cache_count = 0;
    g_log_total_entries = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        uint32_t ym = parse_year_month(de->d_name);
        if (ym == 0) continue;

        if (g_log_cache_count >= LOG_INDEX_MAX) break;

        /* 统计记录数 */
        char sub_path[DB_MAX_PATH];
        snprintf(sub_path, sizeof(sub_path), "%s/%.100s", DB_LOG_DIR, de->d_name);
        long fsize = db_file_size(sub_path);
        int records = (fsize > 0) ? (int)((long)fsize / DB_LOG_ENTRY_SIZE) : 0;

        uint32_t first, last;
        month_to_time_range(ym, &first, &last);

        log_month_cache_t *c = &g_log_cache[g_log_cache_count++];
        c->year_month   = ym;
        c->record_count = (uint32_t)(records > 0 ? records : 0);
        c->first_time   = first;
        c->last_time    = last;
        snprintf(c->filename, sizeof(c->filename), "%.31s", de->d_name);

        g_log_total_entries += (uint32_t)records;
    }
    closedir(d);

    /* 按年月排序 */
    for (int i = 0; i < g_log_cache_count - 1; i++) {
        for (int j = i + 1; j < g_log_cache_count; j++) {
            if (g_log_cache[i].year_month > g_log_cache[j].year_month) {
                log_month_cache_t tmp = g_log_cache[i];
                g_log_cache[i] = g_log_cache[j];
                g_log_cache[j] = tmp;
            }
        }
    }

    return DB_OK;
}

int db_log_get_total_entries(void)
{
    if (g_log_cache_count == 0) log_refresh_cache();
    return (int)g_log_total_entries;
}

/* ================================================================
 *  API 实现
 * ================================================================ */

int service_db_log_append(uint32_t uid, uint8_t event_type,
                          uint8_t match_score, float similarity, uint32_t image_id)
{
    uint32_t now = (uint32_t)time(NULL);

    log_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.timestamp   = now;
    entry.uid         = uid;
    entry.event_type  = event_type;
    entry.match_score = match_score;
    entry.similarity  = similarity;
    entry.image_id    = image_id;

    /* 计算 CRC32（不含 checksum 字段） */
    entry.checksum = db_crc32((const uint8_t *)&entry, sizeof(entry) - 4);

    /* 写入对应月文件 */
    char name[64];
    make_log_name(now, name, sizeof(name));

    int ret = db_write_sync(name, (const uint8_t *)&entry, sizeof(entry), true);
    if (ret != DB_OK) {
        PAL_LOGE(TAG, "写日志失败: %d", ret);
        return ret;
    }

    /* 更新缓存 */
    bool found = false;
    uint32_t ym;
    {
        time_t t = (time_t)now;
        struct tm tm;
        localtime_r(&t, &tm);
        ym = (uint32_t)((tm.tm_year + 1900) * 100 + tm.tm_mon + 1);
    }

    for (int i = 0; i < g_log_cache_count; i++) {
        if (g_log_cache[i].year_month == ym) {
            g_log_cache[i].record_count++;
            if (now < g_log_cache[i].first_time || g_log_cache[i].first_time == 0)
                g_log_cache[i].first_time = now;
            if (now > g_log_cache[i].last_time)
                g_log_cache[i].last_time = now;
            found = true;
            break;
        }
    }

    if (!found) {
        /* 新月份，加入缓存 */
        if (g_log_cache_count < LOG_INDEX_MAX) {
            log_month_cache_t *c = &g_log_cache[g_log_cache_count++];
            c->year_month   = ym;
            c->record_count = 1;
            c->first_time   = now;
            c->last_time    = now;
            snprintf(c->filename, sizeof(c->filename), "%04d%02d.bin",
                     (int)(ym / 100), (int)(ym % 100));
        }
    }
    g_log_total_entries++;
    return DB_OK;
}

int service_db_log_query(uint32_t start_time, uint32_t end_time,
                         uint32_t uid, log_entry_t *list, int max, int *out_total)
{
    if (!list || max <= 0) return DB_ERR_PARAM;
    if (end_time == 0) end_time = (uint32_t)time(NULL) + 86400; /* 默认到明天 */

    /* 确保缓存是最新的 */
    log_refresh_cache();

    int found_total = 0;
    int collected   = 0;

    /* 找出时间范围覆盖的月份文件 */
    for (int m = 0; m < g_log_cache_count; m++) {
        log_month_cache_t *c = &g_log_cache[m];

        /* 月范围与查询范围无交集则跳过 */
        if (c->last_time < start_time || c->first_time > end_time) {
            continue;
        }

        /* 打开月文件顺序扫描 */
        char sub_path[DB_MAX_PATH];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", DB_LOG_DIR, c->filename);
        FILE *f = fopen(sub_path, "rb");
        if (!f) continue;

        log_entry_t entry;
        while (fread(&entry, sizeof(entry), 1, f) == 1) {
            /* 时间过滤 */
            if (entry.timestamp < start_time || entry.timestamp > end_time) {
                continue;
            }
            /* UID 过滤 (0 表示不过滤) */
            if (uid != 0 && entry.uid != uid) {
                continue;
            }

            found_total++;

            if (collected < max) {
                list[collected++] = entry;
            }
        }
        fclose(f);
    }

    if (out_total) *out_total = found_total;
    return DB_OK;
}

int service_db_log_get_recent(log_entry_t *list, int count)
{
    if (!list || count <= 0) return DB_ERR_PARAM;

    log_refresh_cache();

    /* 从最后一个月文件尾部读取 */
    int collected = 0;
    for (int m = g_log_cache_count - 1; m >= 0 && collected < count; m--) {
        log_month_cache_t *c = &g_log_cache[m];
        char sub_path[DB_MAX_PATH];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", DB_LOG_DIR, c->filename);

        FILE *f = fopen(sub_path, "rb");
        if (!f) continue;

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        int records_in_file = (int)((long)fsize / DB_LOG_ENTRY_SIZE);

        int to_read = count - collected;
        if (to_read > records_in_file) to_read = records_in_file;

        /* 从文件倒数第 to_read 条开始读 */
        fseek(f, (long)(records_in_file - to_read) * DB_LOG_ENTRY_SIZE, SEEK_SET);

        log_entry_t entry;
        while (fread(&entry, sizeof(entry), 1, f) == 1 && collected < count) {
            /* 较新的在后，需要倒序？保持时间正序返回 */
            /* 暂时返回时间顺序（旧→新） */
            list[collected++] = entry;
        }
        fclose(f);
    }

    return collected;
}

int service_db_log_cleanup(uint32_t before_time)
{
    int removed = 0;

    log_refresh_cache();

    for (int m = 0; m < g_log_cache_count; m++) {
        log_month_cache_t *c = &g_log_cache[m];

        /* 如果整个月的记录都在阈值之前，直接删文件 */
        if (c->last_time < before_time) {
            char sub_path[DB_MAX_PATH];
            snprintf(sub_path, sizeof(sub_path), "%s/%s", DB_LOG_DIR, c->filename);
            if (db_unlink(sub_path) == DB_OK) {
                PAL_LOGI(TAG, "删除过期日志: %s (%lu 条)",
                         c->filename, (unsigned long)c->record_count);
                g_log_total_entries -= c->record_count;
                c->record_count = 0;
                removed++;
            }
        }
    }

    /* 压缩缓存数组（移除已删除的） */
    int new_count = 0;
    for (int i = 0; i < g_log_cache_count; i++) {
        if (g_log_cache[i].record_count > 0) {
            if (new_count != i) {
                g_log_cache[new_count] = g_log_cache[i];
            }
            new_count++;
        }
    }
    g_log_cache_count = new_count;

    return removed;
}
