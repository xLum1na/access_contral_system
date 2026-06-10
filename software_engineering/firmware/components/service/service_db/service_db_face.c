/**
 * @file    service_db_face.c
 * @brief   人脸特征管理 — face.idx + face.dat + journal
 *
 * 两级存储：
 * - face.idx：定长索引 (64B) 按 UID 排序，支持原地覆盖
 * - face.dat：变长特征 blob 顺序追加，格式 size|data|magic
 * - face.dat.journal：写入中断恢复日志
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

#define TAG "DB_FACE"

/* ================================================================
 *  内部函数声明
 * ================================================================ */

extern int      db_read_all(const char *sub_path, uint8_t **out_data, size_t *out_len);
extern int      db_write_sync(const char *sub_path, const uint8_t *data, size_t len, bool append);
extern int      db_write_atomic(const char *sub_path, const uint8_t *data, size_t len);
extern int      db_unlink(const char *sub_path);
extern bool     db_file_exists(const char *sub_path);
extern long     db_file_size(const char *sub_path);
extern FILE    *db_fopen(const char *sub_path, const char *mode);
extern int      db_fclose(FILE *f);
extern uint32_t db_crc32(const uint8_t *data, size_t len);

/* ---- 索引操作 ---- */
extern int  db_face_index_add(uint32_t uid, const face_index_entry_t *entry);
extern int  db_face_index_get(uint32_t uid, face_index_entry_t *out);
extern int  db_face_index_update(uint32_t uid, const face_index_entry_t *entry);
extern int  db_face_index_delete(uint32_t uid);
extern int  db_face_index_count(void);
extern int  db_face_index_set_feature(uint32_t uid, const uint8_t *feature, uint32_t size);
extern int  db_face_index_get_feature(uint32_t uid, uint8_t *buf, uint32_t buf_size, uint32_t *out_size);
extern bool db_face_index_is_loaded(void);
extern void db_face_index_set_loaded(bool loaded);
extern bool db_face_index_is_dirty(void);
extern void db_face_index_set_dirty(bool dirty);
extern int  db_face_index_iterate(face_iterate_cb_t cb, void *arg);
extern int  db_face_index_get_all_entries(face_index_entry_t **out, int *out_count);

/* ================================================================
 *  face.idx 持久化
 * ================================================================ */

/**
 * @brief 将内存索引全量写回 face.idx
 *
 * 只写非删除的条目。
 */
static int face_idx_sync_to_sd(void)
{
    if (!db_face_index_is_dirty()) return DB_OK;

    face_index_entry_t *entries;
    int count;
    int ret = db_face_index_get_all_entries(&entries, &count);
    if (ret != DB_OK) return ret;

    /* 统计有效条目 */
    int valid = 0;
    for (int i = 0; i < count; i++) {
        if (!(entries[i].flags & DB_FACE_FLAG_DELETED)) valid++;
    }

    if (valid == 0) {
        db_unlink(DB_FACE_IDX);
        db_face_index_set_dirty(false);
        return DB_OK;
    }

    uint8_t *buf = malloc((size_t)valid * DB_FACE_IDX_ENTRY_SIZE);
    if (!buf) return DB_ERR_NO_MEM;

    size_t pos = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].flags & DB_FACE_FLAG_DELETED) continue;
        memcpy(buf + pos, &entries[i], DB_FACE_IDX_ENTRY_SIZE);
        pos += DB_FACE_IDX_ENTRY_SIZE;
    }

    ret = db_write_atomic(DB_FACE_IDX, buf, pos);
    free(buf);

    if (ret == DB_OK) {
        db_face_index_set_dirty(false);
        PAL_LOGD(TAG, "face.idx 写入 %d 条", valid);
    }
    return ret;
}

int db_face_load_idx_from_sd(void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    int ret;

    ret = db_read_all(DB_FACE_IDX, &data, &len);
    if (ret == DB_ERR_NOT_FOUND) {
        PAL_LOGI(TAG, "face.idx 不存在, 从空库启动");
        return DB_OK;
    }
    if (ret != DB_OK || !data) return ret;

    int entry_count = (int)(len / DB_FACE_IDX_ENTRY_SIZE);
    int loaded = 0;

    for (int i = 0; i < entry_count; i++) {
        const face_index_entry_t *e =
            (const face_index_entry_t *)(data + (size_t)i * DB_FACE_IDX_ENTRY_SIZE);

        if (e->flags & DB_FACE_FLAG_DELETED) continue;

        if (db_face_index_add(e->uid, e) != DB_OK) {
            PAL_LOGW(TAG, "添加特征索引失败: uid=%lu", (unsigned long)e->uid);
            continue;
        }
        loaded++;
    }

    free(data);
    db_face_index_set_dirty(false);
    PAL_LOGI(TAG, "face.idx 加载 %d 条 (共 %d 条)", loaded, entry_count);
    return DB_OK;
}

/* ================================================================
 *  face.dat 操作
 * ================================================================ */

/**
 * @brief 从 face.dat 读取单条特征数据
 *
 * 格式: [4B size][N bytes data][4B magic]
 */
static int face_dat_read_entry(uint32_t offset, uint8_t **out_data, uint32_t *out_size)
{
    FILE *f = db_fopen(DB_FACE_DAT, "rb");
    if (!f) return DB_ERR_IO;

    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return DB_ERR_IO;
    }

    uint32_t size;
    if (fread(&size, 1, 4, f) != 4) {
        fclose(f);
        return DB_ERR_FORMAT;
    }

    if (size == 0 || size > 16384) { /* 单特征最大 16KB */
        fclose(f);
        return DB_ERR_FORMAT;
    }

    uint8_t *feature = malloc(size);
    if (!feature) {
        fclose(f);
        return DB_ERR_NO_MEM;
    }

    if (fread(feature, 1, size, f) != size) {
        free(feature);
        fclose(f);
        return DB_ERR_IO;
    }

    /* 校验魔数 */
    uint32_t magic;
    if (fread(&magic, 1, 4, f) != 4 || magic != DB_FACE_MAGIC) {
        free(feature);
        fclose(f);
        return DB_ERR_CHECKSUM;
    }

    fclose(f);

    *out_data = feature;
    *out_size = size;
    return DB_OK;
}

/* ================================================================
 *  Journal 写入（断电安全）
 * ================================================================ */

/**
 * @brief 安全写入特征到 face.dat
 *
 * 流程：
 * 1. 写 face.dat.journal（size + data + magic）
 * 2. fsync journal
 * 3. 追加到 face.dat
 * 4. fsync face.dat
 * 5. 更新 face.idx（RAM + SD）
 * 6. 删除 journal
 *
 * @param feature      特征数据
 * @param feature_size 特征大小
 * @param out_offset   输出 face.dat 中的偏移
 * @return 0 成功
 */
static int face_dat_write_safe(const uint8_t *feature, uint32_t feature_size,
                                uint32_t *out_offset)
{
    if (!feature || feature_size == 0 || feature_size > 16384) return DB_ERR_PARAM;

    int ret;

    /* 获取当前 face.dat 大小作为新记录偏移 */
    long cur_size = db_file_size(DB_FACE_DAT);
    if (cur_size < 0) cur_size = 0;

    /* 构建记录: [4B size][N bytes data][4B magic] */
    uint32_t record_size = 4 + feature_size + 4;
    uint8_t *record = malloc(record_size);
    if (!record) return DB_ERR_NO_MEM;

    size_t pos = 0;
    memcpy(record + pos, &feature_size, 4); pos += 4;
    memcpy(record + pos, feature, feature_size); pos += feature_size;
    uint32_t magic = DB_FACE_MAGIC;
    memcpy(record + pos, &magic, 4); pos += 4;

    /* 步骤 1: 写 journal */
    ret = db_write_sync(DB_FACE_JOURNAL, record, record_size, false);
    if (ret != DB_OK) {
        free(record);
        return ret;
    }

    /* 步骤 3: 追加到 face.dat */
    ret = db_write_sync(DB_FACE_DAT, record, record_size, true);
    if (ret != DB_OK) {
        db_unlink(DB_FACE_JOURNAL);
        free(record);
        return ret;
    }

    /* 步骤 6: 删除 journal */
    db_unlink(DB_FACE_JOURNAL);

    if (out_offset) *out_offset = (uint32_t)cur_size;
    free(record);
    return DB_OK;
}

/* ================================================================
 *  API 实现
 * ================================================================ */

int service_db_face_set(uint32_t uid, const uint8_t *feature,
                        uint32_t size, uint32_t model_id)
{
    if (uid == 0 || !feature || size == 0) return DB_ERR_PARAM;

    uint32_t checksum = db_crc32(feature, size);
    uint32_t offset   = 0;
    uint32_t now      = (uint32_t)time(NULL);

    /* 检查是否已存在 */
    face_index_entry_t existing;
    bool is_update = (db_face_index_get(uid, &existing) == DB_OK);

    /* 安全写入 face.dat */
    int ret = face_dat_write_safe(feature, size, &offset);
    if (ret != DB_OK) {
        PAL_LOGE(TAG, "写入 face.dat 失败: uid=%lu", (unsigned long)uid);
        return ret;
    }

    /* 构建索引条目 */
    face_index_entry_t entry = {
        .uid           = uid,
        .feature_size  = size,
        .feature_offset = offset,
        .checksum      = checksum,
        .created_at    = is_update ? existing.created_at : now,
        .updated_at    = now,
        .model_id      = model_id,
        .flags         = DB_FACE_FLAG_ENABLED,
    };
    memset(entry.name_hash, 0, sizeof(entry.name_hash));
    memset(entry.reserved, 0, sizeof(entry.reserved));

    if (is_update) {
        ret = db_face_index_update(uid, &entry);
    } else {
        ret = db_face_index_add(uid, &entry);
    }
    if (ret != DB_OK) {
        PAL_LOGW(TAG, "更新索引失败: uid=%lu", (unsigned long)uid);
        return ret;
    }

    /* 加载特征到 RAM */
    db_face_index_set_feature(uid, feature, size);

    /* 同步索引到 SD */
    face_idx_sync_to_sd();

    PAL_LOGI(TAG, "%s 特征: uid=%lu size=%lu cs=0x%08lX",
             is_update ? "更新" : "添加",
             (unsigned long)uid, (unsigned long)size, (unsigned long)checksum);
    return DB_OK;
}

int service_db_face_get(uint32_t uid, uint8_t *feature, uint32_t *size)
{
    if (uid == 0 || !feature || !size) return DB_ERR_PARAM;

    face_index_entry_t entry;
    int ret = db_face_index_get(uid, &entry);
    if (ret != DB_OK) return ret;

    if (entry.feature_size > *size) {
        *size = entry.feature_size;
        return DB_ERR_NO_MEM;
    }

    /* 优先从 RAM 获取 */
    if (db_face_index_is_loaded()) {
        uint32_t out_sz = 0;
        ret = db_face_index_get_feature(uid, feature, *size, &out_sz);
        if (ret == DB_OK) {
            *size = out_sz;
            return DB_OK;
        }
    }

    /* 从 face.dat 回退读取 */
    uint8_t *data   = NULL;
    uint32_t out_sz = 0;
    ret = face_dat_read_entry(entry.feature_offset, &data, &out_sz);
    if (ret != DB_OK) return ret;

    if (out_sz > *size) {
        free(data);
        *size = out_sz;
        return DB_ERR_NO_MEM;
    }

    memcpy(feature, data, out_sz);
    *size = out_sz;
    free(data);
    return DB_OK;
}

int service_db_face_delete(uint32_t uid)
{
    if (uid == 0) return DB_ERR_PARAM;

    int ret = db_face_index_delete(uid);
    if (ret != DB_OK) return ret;

    face_idx_sync_to_sd();
    PAL_LOGI(TAG, "删除特征(软): uid=%lu", (unsigned long)uid);
    return DB_OK;
}

int service_db_face_load_all(void)
{
    face_index_entry_t *entries;
    int count;
    int ret = db_face_index_get_all_entries(&entries, &count);
    if (ret != DB_OK) return ret;

    if (db_face_index_is_loaded()) {
        PAL_LOGD(TAG, "特征已加载到 RAM");
        return DB_OK;
    }

    int loaded = 0;
    uint32_t total_bytes = 0;

    for (int i = 0; i < count; i++) {
        if (entries[i].flags & DB_FACE_FLAG_DELETED) continue;
        if (entries[i].feature_size == 0) continue;

        uint8_t *feature  = NULL;
        uint32_t feat_sz  = 0;
        ret = face_dat_read_entry(entries[i].feature_offset, &feature, &feat_sz);
        if (ret != DB_OK) {
            PAL_LOGW(TAG, "读取特征失败 uid=%lu: %d", (unsigned long)entries[i].uid, ret);
            continue;
        }

        ret = db_face_index_set_feature(entries[i].uid, feature, feat_sz);
        free(feature);

        if (ret == DB_OK) {
            loaded++;
            total_bytes += feat_sz;
        }
    }

    db_face_index_set_loaded(true);
    PAL_LOGI(TAG, "加载 %d 条特征到 RAM (%lu KB)", loaded, (unsigned long)(total_bytes / 1024));
    return DB_OK;
}

void service_db_face_unload(void)
{
    /* 重新加载索引时会清空旧特征 */
    db_face_index_set_loaded(false);
    PAL_LOGI(TAG, "特征 RAM 已标记为可释放");
}

int service_db_face_iterate(face_iterate_cb_t cb, void *arg)
{
    if (!cb) return DB_ERR_PARAM;

    if (!db_face_index_is_loaded()) {
        /* 自动加载 */
        int ret = service_db_face_load_all();
        if (ret != DB_OK) return ret;
    }

    return db_face_index_iterate(cb, arg);
}

int service_db_face_count(void)
{
    return db_face_index_count();
}
