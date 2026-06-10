/**
 * @file    service_db_file_io.c
 * @brief   文件 I/O 原子操作：安全写入、fsync 封装、journal 恢复
 *
 * 所有写入操作必须通过本模块，确保断电安全。
 * - 常规文件：write → fsync → (optional rename)
 * - 追加记录：append → fsync → 尾条 CRC 校验（重启时）
 * - 全量替换：write .tmp → fsync → rename → fsync dir
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#include "service_db_internal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "pal_log.h"

#define TAG "DB_IO"

/* ---- 外部，由 service_db.c 设置 ---- */
extern char g_db_base_path[DB_MAX_PATH];

/* ================================================================
 *  路径工具
 * ================================================================ */

int db_make_path(char *buf, size_t buf_size, const char *sub_path)
{
    if (!buf || !sub_path || buf_size < 2) return DB_ERR_PARAM;

    size_t base_len = strlen(g_db_base_path);
    size_t sub_len  = strlen(sub_path);

    if (base_len + sub_len >= buf_size) return DB_ERR_PARAM;

    memcpy(buf, g_db_base_path, base_len);
    memcpy(buf + base_len, sub_path, sub_len + 1); /* 含 \0 */
    return DB_OK;
}

/* ================================================================
 *  目录操作
 * ================================================================ */

/**
 * @brief 递归创建目录（类似 mkdir -p）
 */
static int db_mkdir_p(const char *path)
{
    char tmp[DB_MAX_PATH];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return DB_ERR_PARAM;

    strcpy(tmp, path);

    /* 跳过开头的 / */
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            struct stat st;
            if (stat(tmp, &st) != 0) {
                if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                    PAL_LOGE(TAG, "mkdir(%s): %d", tmp, errno);
                }
            }
            *p = '/';
        }
    }
    /* 最后一级 */
    struct stat st;
    if (stat(tmp, &st) != 0) {
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            PAL_LOGW(TAG, "mkdir(%s): %d", tmp, errno);
        }
    }
    return DB_OK;
}

int db_ensure_dirs(void)
{
    char path[DB_MAX_PATH];

    db_make_path(path, sizeof(path), DB_DIR);
    db_mkdir_p(path);

    db_make_path(path, sizeof(path), DB_LOG_DIR);
    db_mkdir_p(path);

    db_make_path(path, sizeof(path), DB_BAK_DIR);
    db_mkdir_p(path);

    return DB_OK;
}

/* ================================================================
 *  同步写入
 * ================================================================ */

/**
 * @brief 打开文件，失败返回 NULL
 */
FILE *db_fopen(const char *sub_path, const char *mode)
{
    char path[DB_MAX_PATH];
    if (db_make_path(path, sizeof(path), sub_path) != DB_OK) return NULL;

    FILE *f = fopen(path, mode);
    if (!f) {
        PAL_LOGW(TAG, "fopen(%s, %s): %d", path, mode, errno);
    }
    return f;
}

/**
 * @brief fclose + 检查错误
 */
int db_fclose(FILE *f)
{
    if (!f) return DB_ERR_PARAM;
    if (fclose(f) != 0) {
        PAL_LOGW(TAG, "fclose: %d", errno);
        return DB_ERR_IO;
    }
    return DB_OK;
}

/**
 * @brief 写入数据并 fsync
 *
 * @param sub_path 子路径
 * @param data     数据指针
 * @param len      数据长度
 * @param append   true=追加, false=覆盖
 * @return 0 成功
 */
int db_write_sync(const char *sub_path, const uint8_t *data, size_t len, bool append)
{
    if (!sub_path || (!data && len > 0)) return DB_ERR_PARAM;

    char path[DB_MAX_PATH];
    int ret = db_make_path(path, sizeof(path), sub_path);
    if (ret != DB_OK) return ret;

    const char *mode = append ? "ab" : "wb";
    FILE *f = fopen(path, mode);
    if (!f) {
        PAL_LOGE(TAG, "fopen(%s): %d", path, errno);
        return DB_ERR_IO;
    }

    if (len > 0) {
        size_t written = fwrite(data, 1, len, f);
        if (written != len) {
            PAL_LOGE(TAG, "fwrite(%s): %zu/%zu, err=%d", path, written, len, errno);
            fclose(f);
            return DB_ERR_IO;
        }
    }

    /* fsync: 先 sync 文件，再 sync 目录 */
    if (fsync(fileno(f)) != 0) {
        PAL_LOGW(TAG, "fsync(%s): %d", path, errno);
    }

    if (fclose(f) != 0) {
        PAL_LOGW(TAG, "fclose(%s): %d", path, errno);
        return DB_ERR_IO;
    }

    return DB_OK;
}

/**
 * @brief 原子替换写入：
 *        写入 .tmp → fsync → rename → 删除旧的
 *
 * @param sub_path     目标文件子路径
 * @param data         数据指针
 * @param len          数据长度
 * @return 0 成功
 */
int db_write_atomic(const char *sub_path, const uint8_t *data, size_t len)
{
    if (!sub_path) return DB_ERR_PARAM;

    char tmp_path[DB_MAX_PATH];
    char real_path[DB_MAX_PATH];

    /* 构造 .tmp 路径 */
    size_t sub_len = strlen(sub_path);
    if (sub_len + 5 >= DB_MAX_PATH) return DB_ERR_PARAM;
    memcpy(tmp_path, g_db_base_path, strlen(g_db_base_path));
    memcpy(tmp_path + strlen(g_db_base_path), sub_path, sub_len);
    memcpy(tmp_path + strlen(g_db_base_path) + sub_len, ".tmp", 5);

    memcpy(real_path, g_db_base_path, strlen(g_db_base_path));
    memcpy(real_path + strlen(g_db_base_path), sub_path, sub_len + 1);

    /* 步骤 1: 写 .tmp */
    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        PAL_LOGE(TAG, "atomic fopen(%s): %d", tmp_path, errno);
        return DB_ERR_IO;
    }

    if (len > 0) {
        if (fwrite(data, 1, len, f) != len) {
            PAL_LOGE(TAG, "atomic fwrite(%s): %d", tmp_path, errno);
            fclose(f);
            unlink(tmp_path);
            return DB_ERR_IO;
        }
    }

    if (fsync(fileno(f)) != 0) {
        PAL_LOGW(TAG, "atomic fsync(%s): %d", tmp_path, errno);
    }
    fclose(f);

    /* 步骤 2: rename（FAT 上是原子的） */
    if (rename(tmp_path, real_path) != 0) {
        PAL_LOGE(TAG, "rename(%s -> %s): %d", tmp_path, real_path, errno);
        unlink(tmp_path);
        return DB_ERR_IO;
    }

    PAL_LOGD(TAG, "atomic write OK: %s", sub_path);
    return DB_OK;
}

/**
 * @brief 读取文件全部内容到内存（调用者负责释放）
 *
 * @param sub_path  子路径
 * @param out_data  输出数据指针 (malloc)
 * @param out_len   输出数据长度
 * @return 0 成功，DB_ERR_NOT_FOUND 文件不存在
 */
int db_read_all(const char *sub_path, uint8_t **out_data, size_t *out_len)
{
    if (!sub_path || !out_data || !out_len) return DB_ERR_PARAM;

    *out_data = NULL;
    *out_len  = 0;

    char path[DB_MAX_PATH];
    int ret = db_make_path(path, sizeof(path), sub_path);
    if (ret != DB_OK) return ret;

    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) return DB_ERR_NOT_FOUND;
        PAL_LOGE(TAG, "stat(%s): %d", path, errno);
        return DB_ERR_IO;
    }

    if (st.st_size == 0) {
        *out_data = malloc(1); /* 空文件返回空 buffer */
        *out_len = 0;
        return DB_OK;
    }

    uint8_t *buf = malloc((size_t)st.st_size);
    if (!buf) return DB_ERR_NO_MEM;

    FILE *f = fopen(path, "rb");
    if (!f) {
        PAL_LOGE(TAG, "read fopen(%s): %d", path, errno);
        free(buf);
        return DB_ERR_IO;
    }

    size_t read = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);

    if (read != (size_t)st.st_size) {
        PAL_LOGW(TAG, "read mismatch: %zu/%ld", read, st.st_size);
        free(buf);
        return DB_ERR_IO;
    }

    *out_data = buf;
    *out_len  = read;
    return DB_OK;
}

/**
 * @brief 删除文件
 */
int db_unlink(const char *sub_path)
{
    char path[DB_MAX_PATH];
    int ret = db_make_path(path, sizeof(path), sub_path);
    if (ret != DB_OK) return ret;

    if (unlink(path) != 0 && errno != ENOENT) {
        PAL_LOGW(TAG, "unlink(%s): %d", path, errno);
        return DB_ERR_IO;
    }
    return DB_OK;
}

/**
 * @brief 检查文件是否存在
 */
bool db_file_exists(const char *sub_path)
{
    char path[DB_MAX_PATH];
    if (db_make_path(path, sizeof(path), sub_path) != DB_OK) return false;

    struct stat st;
    return stat(path, &st) == 0;
}

/**
 * @brief 获取文件大小，文件不存在返回 -1
 */
long db_file_size(const char *sub_path)
{
    char path[DB_MAX_PATH];
    if (db_make_path(path, sizeof(path), sub_path) != DB_OK) return -1;

    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

/**
 * @brief 打开目录迭代器
 */
DIR *db_opendir(const char *sub_path)
{
    char path[DB_MAX_PATH];
    if (db_make_path(path, sizeof(path), sub_path) != DB_OK) return NULL;
    return opendir(path);
}

/* ================================================================
 *  Journal 恢复
 * ================================================================ */

/**
 * @brief 检查并恢复 face.dat journal
 *
 * 场景：写入过程中断电，journal 文件残留。
 * journal 格式：[4B size][N bytes feature data][4B magic]
 *
 * @return 0=无 journal 或已恢复，负数=恢复失败
 */
int db_recover_journal(void)
{
    uint8_t *jdata = NULL;
    size_t   jlen  = 0;
    int ret;

    ret = db_read_all(DB_FACE_JOURNAL, &jdata, &jlen);
    if (ret == DB_ERR_NOT_FOUND) {
        return DB_OK; /* 没有未完成的 journal，正常 */
    }
    if (ret != DB_OK || !jdata) {
        PAL_LOGW(TAG, "读取 journal 失败: %d", ret);
        db_unlink(DB_FACE_JOURNAL);
        return DB_ERR_JOURNAL;
    }

    if (jlen < 8) {
        PAL_LOGW(TAG, "journal 太短 (%zu 字节), 丢弃", jlen);
        free(jdata);
        db_unlink(DB_FACE_JOURNAL);
        return DB_OK;
    }

    /* 校验 journal 魔数 */
    if (jlen < 4) {
        free(jdata);
        db_unlink(DB_FACE_JOURNAL);
        return DB_OK;
    }

    uint32_t magic;
    memcpy(&magic, jdata + jlen - 4, 4);

    if (magic == DB_FACE_MAGIC) {
        /* journal 完整，重放到 face.dat */
        PAL_LOGI(TAG, "重放 journal (%zu 字节)", jlen);
        ret = db_write_sync(DB_FACE_DAT, jdata, jlen, true);
        if (ret == DB_OK) {
            PAL_LOGI(TAG, "journal 重放成功");
        } else {
            PAL_LOGE(TAG, "journal 重放失败: %d", ret);
        }
    } else {
        PAL_LOGW(TAG, "journal 魔数不匹配 (0x%08lX), 丢弃", (unsigned long)magic);
    }

    free(jdata);
    db_unlink(DB_FACE_JOURNAL);
    return DB_OK;
}

/* ================================================================
 *  CRC32
 * ================================================================ */

uint32_t db_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            uint32_t mask = (crc & 1) ? 0xFFFFFFFF : 0;
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/* ================================================================
 *  简单哈希 — djb2
 * ================================================================ */

uint64_t db_hash_string(const char *str)
{
    uint64_t hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}
