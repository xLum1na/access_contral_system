/**
 * @file    service_db_index.c
 * @brief   RAM 常驻索引：用户索引 + 人脸特征索引
 *
 * - 用户索引：按 UID 排序数组，二分查找 O(log N)
 * - 特征索引：定长索引数组 + 特征数据指针数组，支持分片加载
 *
 * 内部模块，不直接暴露给 API 层。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#include "service_db_internal.h"
#include "osal_mutex.h"
#include "osal_memory.h"
#include "pal_log.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

#define TAG "DB_IDX"

/* ================================================================
 *  用户索引
 * ================================================================ */

typedef struct {
    uint32_t       *uids;        /**< 排序后的 UID 数组 */
    user_record_t  *records;     /**< 用户记录数组（与 uids 索引对应） */
    int             count;       /**< 当前记录数 */
    int             capacity;    /**< 分配容量 */
    uint32_t        next_uid;    /**< 下一个可用 UID */
    osal_mutex_t    lock;        /**< 并发保护 */
} user_index_t;

static user_index_t g_user_idx = {0};

/* ---- 二分查找 ---- */

/**
 * @brief 二分查找 uid，返回下标（-1=未找到）
 */
static int user_idx_bsearch(uint32_t uid)
{
    if (!g_user_idx.uids || g_user_idx.count == 0) return -1;

    int lo = 0, hi = g_user_idx.count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_user_idx.uids[mid] == uid) return mid;
        if (g_user_idx.uids[mid] < uid) lo = mid + 1;
        else                           hi = mid - 1;
    }
    return -1;
}

/**
 * @brief 找到 uid 的插入位置（保持升序）
 */
static int user_idx_find_insert_pos(uint32_t uid)
{
    int lo = 0, hi = g_user_idx.count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_user_idx.uids[mid] < uid) lo = mid + 1;
        else                            hi = mid;
    }
    return lo;
}

/* ---- 初始化 ---- */

int db_user_index_init(void)
{
    if (g_user_idx.uids) return DB_OK; /* 已初始化 */

    g_user_idx.lock = osal_mutex_create();
    if (!g_user_idx.lock) return DB_ERR_NO_MEM;

    g_user_idx.capacity = DB_USER_MAX_COUNT;
    g_user_idx.count    = 0;
    g_user_idx.next_uid = 1;

    /* PSRAM 分配：UID 数组 + 记录数组 */
    g_user_idx.uids = osal_malloc_psram(sizeof(uint32_t) * (size_t)g_user_idx.capacity);
    if (!g_user_idx.uids) goto fail;

    g_user_idx.records = osal_malloc_psram(sizeof(user_record_t) * (size_t)g_user_idx.capacity);
    if (!g_user_idx.records) goto fail;

    PAL_LOGI(TAG, "用户索引初始化: capacity=%d", g_user_idx.capacity);
    return DB_OK;

fail:
    if (g_user_idx.uids) { osal_free(g_user_idx.uids); g_user_idx.uids = NULL; }
    if (g_user_idx.records) { osal_free(g_user_idx.records); g_user_idx.records = NULL; }
    osal_mutex_delete(g_user_idx.lock);
    g_user_idx.lock = NULL;
    return DB_ERR_NO_MEM;
}

void db_user_index_deinit(void)
{
    if (g_user_idx.lock) osal_mutex_lock(g_user_idx.lock, 5000);

    if (g_user_idx.uids) { osal_free(g_user_idx.uids); g_user_idx.uids = NULL; }
    if (g_user_idx.records) { osal_free(g_user_idx.records); g_user_idx.records = NULL; }
    g_user_idx.count    = 0;
    g_user_idx.capacity = 0;
    g_user_idx.next_uid = 0;

    if (g_user_idx.lock) {
        osal_mutex_unlock(g_user_idx.lock);
        osal_mutex_delete(g_user_idx.lock);
        g_user_idx.lock = NULL;
    }
}

/* ---- CRUD ---- */

int db_user_index_add(uint32_t uid, const user_record_t *record)
{
    if (!g_user_idx.lock || !record) return DB_ERR_NOT_INIT;
    if (g_user_idx.count >= g_user_idx.capacity) return DB_ERR_FULL;

    osal_mutex_lock(g_user_idx.lock, 5000);

    if (user_idx_bsearch(uid) >= 0) {
        osal_mutex_unlock(g_user_idx.lock);
        return DB_ERR_DUPLICATE;
    }

    int pos = user_idx_find_insert_pos(uid);

    /* 将 pos 之后的元素后移 */
    if (pos < g_user_idx.count) {
        memmove(&g_user_idx.uids[pos + 1], &g_user_idx.uids[pos],
                (size_t)(g_user_idx.count - pos) * sizeof(uint32_t));
        memmove(&g_user_idx.records[pos + 1], &g_user_idx.records[pos],
                (size_t)(g_user_idx.count - pos) * sizeof(user_record_t));
    }

    g_user_idx.uids[pos]    = uid;
    g_user_idx.records[pos] = *record;
    g_user_idx.count++;

    if (uid >= g_user_idx.next_uid) {
        g_user_idx.next_uid = uid + 1;
    }

    osal_mutex_unlock(g_user_idx.lock);
    return DB_OK;
}

int db_user_index_get(uint32_t uid, user_record_t *out)
{
    if (!g_user_idx.lock || !out) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_user_idx.lock, 5000);
    int pos = user_idx_bsearch(uid);
    if (pos < 0) {
        osal_mutex_unlock(g_user_idx.lock);
        return DB_ERR_NOT_FOUND;
    }
    *out = g_user_idx.records[pos];
    osal_mutex_unlock(g_user_idx.lock);
    return DB_OK;
}

int db_user_index_update(uint32_t uid, const user_record_t *record)
{
    if (!g_user_idx.lock || !record) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_user_idx.lock, 5000);
    int pos = user_idx_bsearch(uid);
    if (pos < 0) {
        osal_mutex_unlock(g_user_idx.lock);
        return DB_ERR_NOT_FOUND;
    }
    g_user_idx.records[pos] = *record;
    osal_mutex_unlock(g_user_idx.lock);
    return DB_OK;
}

int db_user_index_delete(uint32_t uid)
{
    if (!g_user_idx.lock) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_user_idx.lock, 5000);
    int pos = user_idx_bsearch(uid);
    if (pos < 0) {
        osal_mutex_unlock(g_user_idx.lock);
        return DB_ERR_NOT_FOUND;
    }

    /* 标记为删除 */
    g_user_idx.records[pos].flags |= DB_USER_FLAG_DELETED;
    g_user_idx.records[pos].updated = (uint32_t)time(NULL);
    osal_mutex_unlock(g_user_idx.lock);
    return DB_OK;
}

int db_user_index_find_by_name(const char *name, user_record_t *out)
{
    if (!g_user_idx.lock || !name || !out) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_user_idx.lock, 5000);
    for (int i = 0; i < g_user_idx.count; i++) {
        if (strcmp(g_user_idx.records[i].name, name) == 0) {
            *out = g_user_idx.records[i];
            osal_mutex_unlock(g_user_idx.lock);
            return DB_OK;
        }
    }
    osal_mutex_unlock(g_user_idx.lock);
    return DB_ERR_NOT_FOUND;
}

int db_user_index_list(user_record_t *list, int max, int *out_total)
{
    if (!g_user_idx.lock || !list || max <= 0) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_user_idx.lock, 5000);
    int n = 0;
    for (int i = 0; i < g_user_idx.count && n < max; i++) {
        if (g_user_idx.records[i].flags & DB_USER_FLAG_DELETED) continue;
        list[n++] = g_user_idx.records[i];
    }
    if (out_total) *out_total = g_user_idx.count;
    osal_mutex_unlock(g_user_idx.lock);
    return DB_OK;
}

int db_user_index_count(void)
{
    if (!g_user_idx.lock) return 0;
    osal_mutex_lock(g_user_idx.lock, 5000);
    int n = 0;
    for (int i = 0; i < g_user_idx.count; i++) {
        if (!(g_user_idx.records[i].flags & DB_USER_FLAG_DELETED)) n++;
    }
    osal_mutex_unlock(g_user_idx.lock);
    return n;
}

uint32_t db_user_index_next_uid(void)
{
    if (!g_user_idx.lock) return 0;
    return g_user_idx.next_uid;
}

/* ================================================================
 *  特征索引
 * ================================================================ */

typedef struct {
    face_index_entry_t *entries;     /**< 特征索引数组 (PSRAM) */
    uint8_t           **features;    /**< 特征数据指针数组 (PSRAM) */
    int                 count;       /**< 当前记录数 */
    int                 capacity;    /**< 分配容量 */
    bool                loaded;      /**< 特征数据是否已加载到 RAM */
    bool                dirty;       /**< 索引是否脏（需要写回） */
    osal_mutex_t        lock;
} face_index_t;

static face_index_t g_face_idx = {0};

/* ---- 二分查找 ---- */

static int face_idx_bsearch(uint32_t uid)
{
    if (!g_face_idx.entries || g_face_idx.count == 0) return -1;

    int lo = 0, hi = g_face_idx.count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_face_idx.entries[mid].uid == uid) return mid;
        if (g_face_idx.entries[mid].uid < uid) lo = mid + 1;
        else                                   hi = mid - 1;
    }
    return -1;
}

static int face_idx_insert_pos(uint32_t uid)
{
    int lo = 0, hi = g_face_idx.count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_face_idx.entries[mid].uid < uid) lo = mid + 1;
        else                                   hi = mid;
    }
    return lo;
}

/* ---- 初始化 ---- */

int db_face_index_init(void)
{
    if (g_face_idx.entries) return DB_OK;

    g_face_idx.lock = osal_mutex_create();
    if (!g_face_idx.lock) return DB_ERR_NO_MEM;

    g_face_idx.capacity = DB_USER_MAX_COUNT;
    g_face_idx.count    = 0;
    g_face_idx.loaded   = false;
    g_face_idx.dirty    = false;

    g_face_idx.entries = osal_malloc_psram(sizeof(face_index_entry_t) * (size_t)g_face_idx.capacity);
    if (!g_face_idx.entries) goto fail;

    g_face_idx.features = osal_calloc_caps((size_t)g_face_idx.capacity,
                                           sizeof(uint8_t *),
                                           OSAL_MEM_CAP_PSRAM);
    if (!g_face_idx.features) goto fail;

    PAL_LOGI(TAG, "特征索引初始化: capacity=%d", g_face_idx.capacity);
    return DB_OK;

fail:
    if (g_face_idx.entries) { osal_free(g_face_idx.entries); g_face_idx.entries = NULL; }
    if (g_face_idx.features) { osal_free(g_face_idx.features); g_face_idx.features = NULL; }
    osal_mutex_delete(g_face_idx.lock);
    g_face_idx.lock = NULL;
    return DB_ERR_NO_MEM;
}

void db_face_index_deinit(void)
{
    if (g_face_idx.lock) osal_mutex_lock(g_face_idx.lock, 5000);

    /* 释放所有特征数据 */
    for (int i = 0; i < g_face_idx.count; i++) {
        if (g_face_idx.features[i]) {
            osal_free(g_face_idx.features[i]);
            g_face_idx.features[i] = NULL;
        }
    }

    if (g_face_idx.entries) { osal_free(g_face_idx.entries); g_face_idx.entries = NULL; }
    if (g_face_idx.features) { osal_free(g_face_idx.features); g_face_idx.features = NULL; }
    g_face_idx.count  = 0;
    g_face_idx.loaded = false;

    if (g_face_idx.lock) {
        osal_mutex_unlock(g_face_idx.lock);
        osal_mutex_delete(g_face_idx.lock);
        g_face_idx.lock = NULL;
    }
}

/* ---- CRUD ---- */

int db_face_index_add(uint32_t uid, const face_index_entry_t *entry)
{
    if (!g_face_idx.lock || !entry) return DB_ERR_NOT_INIT;
    if (g_face_idx.count >= g_face_idx.capacity) return DB_ERR_FULL;

    osal_mutex_lock(g_face_idx.lock, 5000);

    if (face_idx_bsearch(uid) >= 0) {
        osal_mutex_unlock(g_face_idx.lock);
        return DB_ERR_DUPLICATE;
    }

    int pos = face_idx_insert_pos(uid);
    if (pos < g_face_idx.count) {
        memmove(&g_face_idx.entries[pos + 1], &g_face_idx.entries[pos],
                (size_t)(g_face_idx.count - pos) * sizeof(face_index_entry_t));
        memmove(&g_face_idx.features[pos + 1], &g_face_idx.features[pos],
                (size_t)(g_face_idx.count - pos) * sizeof(uint8_t *));
    }

    g_face_idx.entries[pos] = *entry;
    g_face_idx.features[pos] = NULL;
    g_face_idx.count++;
    g_face_idx.dirty = true;

    osal_mutex_unlock(g_face_idx.lock);
    return DB_OK;
}

int db_face_index_get(uint32_t uid, face_index_entry_t *out)
{
    if (!g_face_idx.lock || !out) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_face_idx.lock, 5000);
    int pos = face_idx_bsearch(uid);
    if (pos < 0) {
        osal_mutex_unlock(g_face_idx.lock);
        return DB_ERR_NOT_FOUND;
    }
    *out = g_face_idx.entries[pos];
    osal_mutex_unlock(g_face_idx.lock);
    return DB_OK;
}

int db_face_index_update(uint32_t uid, const face_index_entry_t *entry)
{
    if (!g_face_idx.lock || !entry) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_face_idx.lock, 5000);
    int pos = face_idx_bsearch(uid);
    if (pos < 0) {
        osal_mutex_unlock(g_face_idx.lock);
        return DB_ERR_NOT_FOUND;
    }
    g_face_idx.entries[pos] = *entry;
    g_face_idx.dirty = true;
    osal_mutex_unlock(g_face_idx.lock);
    return DB_OK;
}

int db_face_index_delete(uint32_t uid)
{
    if (!g_face_idx.lock) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_face_idx.lock, 5000);
    int pos = face_idx_bsearch(uid);
    if (pos < 0) {
        osal_mutex_unlock(g_face_idx.lock);
        return DB_ERR_NOT_FOUND;
    }

    g_face_idx.entries[pos].flags |= DB_FACE_FLAG_DELETED;
    g_face_idx.dirty = true;

    /* 释放特征内存 */
    if (g_face_idx.features[pos]) {
        osal_free(g_face_idx.features[pos]);
        g_face_idx.features[pos] = NULL;
    }
    osal_mutex_unlock(g_face_idx.lock);
    return DB_OK;
}

int db_face_index_count(void)
{
    if (!g_face_idx.lock) return 0;
    osal_mutex_lock(g_face_idx.lock, 5000);
    int n = 0;
    for (int i = 0; i < g_face_idx.count; i++) {
        if (!(g_face_idx.entries[i].flags & DB_FACE_FLAG_DELETED)) n++;
    }
    osal_mutex_unlock(g_face_idx.lock);
    return n;
}

/* ---- 特征数据 RAM 管理 ---- */

int db_face_index_set_feature(uint32_t uid, const uint8_t *feature, uint32_t size)
{
    if (!g_face_idx.lock || !feature) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_face_idx.lock, 5000);
    int pos = face_idx_bsearch(uid);
    if (pos < 0) {
        osal_mutex_unlock(g_face_idx.lock);
        return DB_ERR_NOT_FOUND;
    }

    /* 先释放旧数据 */
    if (g_face_idx.features[pos]) {
        osal_free(g_face_idx.features[pos]);
        g_face_idx.features[pos] = NULL;
    }

    if (size > 0) {
        g_face_idx.features[pos] = osal_malloc_psram(size);
        if (!g_face_idx.features[pos]) {
            osal_mutex_unlock(g_face_idx.lock);
            return DB_ERR_NO_MEM;
        }
        memcpy(g_face_idx.features[pos], feature, size);
    }
    osal_mutex_unlock(g_face_idx.lock);
    return DB_OK;
}

int db_face_index_get_feature(uint32_t uid, uint8_t *buf, uint32_t buf_size,
                              uint32_t *out_size)
{
    if (!g_face_idx.lock || !buf || !out_size) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_face_idx.lock, 5000);
    int pos = face_idx_bsearch(uid);
    if (pos < 0) {
        osal_mutex_unlock(g_face_idx.lock);
        return DB_ERR_NOT_FOUND;
    }

    if (!g_face_idx.features[pos]) {
        osal_mutex_unlock(g_face_idx.lock);
        return DB_ERR_NOT_FOUND; /* 特征未加载到 RAM */
    }

    uint32_t sz = g_face_idx.entries[pos].feature_size;
    if (sz > buf_size) {
        osal_mutex_unlock(g_face_idx.lock);
        return DB_ERR_NO_MEM;
    }

    memcpy(buf, g_face_idx.features[pos], sz);
    *out_size = sz;
    osal_mutex_unlock(g_face_idx.lock);
    return DB_OK;
}

bool db_face_index_is_loaded(void)
{
    return g_face_idx.loaded;
}

void db_face_index_set_loaded(bool loaded)
{
    g_face_idx.loaded = loaded;
}

bool db_face_index_is_dirty(void)
{
    return g_face_idx.dirty;
}

void db_face_index_set_dirty(bool dirty)
{
    g_face_idx.dirty = dirty;
}

int db_face_index_get_all_entries(face_index_entry_t **out, int *out_count)
{
    if (!g_face_idx.lock || !out || !out_count) return DB_ERR_NOT_INIT;

    *out = g_face_idx.entries;
    *out_count = g_face_idx.count;
    return DB_OK;
}

int db_face_index_iterate(face_iterate_cb_t cb, void *arg)
{
    if (!g_face_idx.lock || !cb) return DB_ERR_NOT_INIT;

    osal_mutex_lock(g_face_idx.lock, 5000);

    for (int i = 0; i < g_face_idx.count; i++) {
        face_index_entry_t *e = &g_face_idx.entries[i];
        if (e->flags & DB_FACE_FLAG_DELETED) continue;
        if (!g_face_idx.features[i]) continue;

        int ret = cb(e->uid, g_face_idx.features[i], e->feature_size, arg);
        if (ret != 0) {
            osal_mutex_unlock(g_face_idx.lock);
            return ret;
        }
    }

    osal_mutex_unlock(g_face_idx.lock);
    return DB_OK;
}
