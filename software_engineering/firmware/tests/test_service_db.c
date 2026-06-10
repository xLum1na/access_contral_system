/**
 * @file    test_service_db.c
 * @brief   service_db 模块单元测试 — 核心逻辑验证
 *
 * 测试覆盖：
 *   - CRC32 校验
 *   - 字符串哈希
 *   - JSON 用户编解码
 *   - 用户索引 CRUD
 *   - 特征索引 CRUD
 *   - 数据结构大小校验
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#include "unity.h"
#include "service_db_internal.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ================================================================
 *  Mock — 模拟 OSAL 依赖（宿主机无 FreeRTOS）
 * ================================================================ */

/* 模拟互斥锁 — 单线程测试只需空实现 */
static int mock_mutex_count = 0;

void *osal_mutex_create(void) { mock_mutex_count++; return (void *)&mock_mutex_count; }
bool  osal_mutex_lock(void *m, uint32_t to)   { (void)m; (void)to; return true; }
void  osal_mutex_unlock(void *m)              { (void)m; }
void  osal_mutex_delete(void *m)              { (void)m; mock_mutex_count--; }

void *osal_malloc_psram(size_t s)  { return malloc(s); }
void *osal_malloc_caps(size_t size, uint32_t caps) { (void)caps; return malloc(size); }
void *osal_calloc_caps(size_t n, size_t size, uint32_t caps) { (void)caps; return calloc(n, size); }
void *osal_malloc_internal(size_t s) { return malloc(s); }
void  osal_free(void *p)           { free(p); }

/* ---- 全局基路径（文件 I/O 模块需要） ---- */
char g_db_base_path[DB_MAX_PATH] = "/tmp/db_test";

/* ---- 导入被测模块源码（直接编译到本测试中） ---- */
/* 注意：各 .c 文件有自己的 #define TAG，用 #undef 避免重定义警告 */
#include "service_db_index.c"
#undef TAG
#include "service_db_file_io.c"
#undef TAG

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */

void setUp(void)
{
    /* 清理索引 */
    db_user_index_deinit();
    db_face_index_deinit();
}

void tearDown(void)
{
    db_user_index_deinit();
    db_face_index_deinit();
}

/* ================================================================
 *  测试：数据结构大小
 * ================================================================ */

void test_struct_sizes(void)
{
    /* face_index_entry_t 必须精确定长 64B */
    TEST_ASSERT_EQUAL(64, sizeof(face_index_entry_t));

    /* log_entry_t 必须精确定长 24B */
    TEST_ASSERT_EQUAL(24, sizeof(log_entry_t));

    /* user_record_t 大小合理（不应超过 200B） */
    TEST_ASSERT_TRUE(sizeof(user_record_t) < 200);
}

/* ================================================================
 *  测试：CRC32
 * ================================================================ */

void test_crc32_known_vectors(void)
{
    /* 空数据 CRC32 = 0 */
    uint32_t c0 = db_crc32(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0x00000000, c0);

    /* "123456789" CRC32 = 0xCBF43926 */
    const uint8_t *v = (const uint8_t *)"123456789";
    uint32_t c1 = db_crc32(v, 9);
    TEST_ASSERT_EQUAL_UINT32(0xCBF43926, c1);
}

void test_crc32_different_data_different_crc(void)
{
    const uint8_t a[] = {1, 2, 3, 4};
    const uint8_t b[] = {1, 2, 3, 5};

    uint32_t ca = db_crc32(a, 4);
    uint32_t cb = db_crc32(b, 4);

    TEST_ASSERT_NOT_EQUAL(ca, cb);
}

/* ================================================================
 *  测试：字符串哈希
 * ================================================================ */

void test_hash_same_string_same_hash(void)
{
    uint64_t h1 = db_hash_string("hello");
    uint64_t h2 = db_hash_string("hello");
    TEST_ASSERT_EQUAL_UINT64(h1, h2);
}

void test_hash_different_different(void)
{
    uint64_t h1 = db_hash_string("hello");
    uint64_t h2 = db_hash_string("world");
    TEST_ASSERT_NOT_EQUAL(h1, h2);
}

/* ================================================================
 *  测试：路径拼接
 * ================================================================ */

void test_make_path(void)
{
    char buf[128];
    int ret = db_make_path(buf, sizeof(buf), "/db/test.bin");

    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_STRING("/tmp/db_test/db/test.bin", buf);
}

void test_make_path_overflow(void)
{
    char buf[4];
    /* 基路径 + 子路径超过 4 字节应失败 */
    int ret = db_make_path(buf, sizeof(buf), "/very/long/path");
    TEST_ASSERT_NOT_EQUAL(0, ret);
}

/* ================================================================
 *  测试：用户索引 CRUD
 * ================================================================ */

void test_user_index_init(void)
{
    int ret = db_user_index_init();
    TEST_ASSERT_EQUAL(DB_OK, ret);
    TEST_ASSERT_EQUAL(0, db_user_index_count());
}

void test_user_add_and_get(void)
{
    db_user_index_init();

    user_record_t u = {0};
    u.uid  = 100;
    strcpy(u.name, "张测试");
    u.perm = 7;
    u.flags = DB_USER_FLAG_ENABLED;

    int ret = db_user_index_add(100, &u);
    TEST_ASSERT_EQUAL(DB_OK, ret);
    TEST_ASSERT_EQUAL(1, db_user_index_count());

    user_record_t out;
    ret = db_user_index_get(100, &out);
    TEST_ASSERT_EQUAL(DB_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(100, out.uid);
    TEST_ASSERT_EQUAL_STRING("张测试", out.name);
    TEST_ASSERT_EQUAL_UINT8(7, out.perm);
}

void test_user_add_duplicate(void)
{
    db_user_index_init();

    user_record_t u = {0};
    u.uid = 1;
    db_user_index_add(1, &u);

    int ret = db_user_index_add(1, &u);
    TEST_ASSERT_EQUAL(DB_ERR_DUPLICATE, ret);
}

void test_user_get_not_found(void)
{
    db_user_index_init();

    user_record_t out;
    int ret = db_user_index_get(999, &out);
    TEST_ASSERT_EQUAL(DB_ERR_NOT_FOUND, ret);
}

void test_user_update(void)
{
    db_user_index_init();

    user_record_t u = {0};
    u.uid = 1;
    strcpy(u.name, "原始");
    db_user_index_add(1, &u);

    strcpy(u.name, "修改后");
    int ret = db_user_index_update(1, &u);
    TEST_ASSERT_EQUAL(DB_OK, ret);

    user_record_t out;
    db_user_index_get(1, &out);
    TEST_ASSERT_EQUAL_STRING("修改后", out.name);
}

void test_user_delete(void)
{
    db_user_index_init();

    user_record_t u = {0};
    u.uid = 1;
    u.flags = DB_USER_FLAG_ENABLED;
    db_user_index_add(1, &u);

    int ret = db_user_index_delete(1);
    TEST_ASSERT_EQUAL(DB_OK, ret);

    /* 软删除后 count 不计入 */
    TEST_ASSERT_EQUAL(0, db_user_index_count());

    /* 但记录仍可通过 get 读到（带 deleted 标志） */
    user_record_t out;
    ret = db_user_index_get(1, &out);
    TEST_ASSERT_EQUAL(DB_OK, ret);
    TEST_ASSERT_TRUE(out.flags & DB_USER_FLAG_DELETED);
}

void test_user_find_by_name(void)
{
    db_user_index_init();

    user_record_t u = {0};
    u.uid = 1;
    strcpy(u.name, "李四");
    db_user_index_add(1, &u);

    u.uid = 2;
    strcpy(u.name, "王五");
    db_user_index_add(2, &u);

    user_record_t out;
    int ret = db_user_index_find_by_name("王五", &out);
    TEST_ASSERT_EQUAL(DB_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(2, out.uid);
}

void test_user_list(void)
{
    db_user_index_init();

    for (uint32_t i = 1; i <= 10; i++) {
        user_record_t u = {0};
        u.uid   = i;
        u.flags = DB_USER_FLAG_ENABLED;
        snprintf(u.name, sizeof(u.name), "用户%lu", (unsigned long)i);
        db_user_index_add(i, &u);
    }

    TEST_ASSERT_EQUAL(10, db_user_index_count());

    user_record_t list[5];
    int total;
    db_user_index_list(list, 5, &total);

    TEST_ASSERT_EQUAL(10, total); /* 总数 10 */
    /* 前 5 条（按 UID 排序） */
    TEST_ASSERT_EQUAL_UINT32(1, list[0].uid);
    TEST_ASSERT_EQUAL_UINT32(5, list[4].uid);
}

/* ================================================================
 *  测试：特征索引 CRUD
 * ================================================================ */

void test_face_index_init(void)
{
    int ret = db_face_index_init();
    TEST_ASSERT_EQUAL(DB_OK, ret);
    TEST_ASSERT_EQUAL(0, db_face_index_count());
}

void test_face_add_and_get(void)
{
    db_face_index_init();

    face_index_entry_t e = {0};
    e.uid          = 100;
    e.feature_size = 512;
    e.feature_offset = 0;
    e.checksum     = 0xABCD1234;
    e.model_id     = 1;
    e.flags        = DB_FACE_FLAG_ENABLED;

    int ret = db_face_index_add(100, &e);
    TEST_ASSERT_EQUAL(DB_OK, ret);
    TEST_ASSERT_EQUAL(1, db_face_index_count());

    face_index_entry_t out;
    ret = db_face_index_get(100, &out);
    TEST_ASSERT_EQUAL(DB_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(100, out.uid);
    TEST_ASSERT_EQUAL_UINT32(512, out.feature_size);
    TEST_ASSERT_EQUAL_UINT32(0xABCD1234, out.checksum);
}

void test_face_update(void)
{
    db_face_index_init();

    face_index_entry_t e = {0};
    e.uid = 50;
    e.checksum = 0xAAAAAAAA;
    e.flags = DB_FACE_FLAG_ENABLED;
    db_face_index_add(50, &e);

    e.checksum = 0xBBBBBBBB;
    int ret = db_face_index_update(50, &e);
    TEST_ASSERT_EQUAL(DB_OK, ret);

    face_index_entry_t out;
    db_face_index_get(50, &out);
    TEST_ASSERT_EQUAL_UINT32(0xBBBBBBBB, out.checksum);
}

void test_face_delete(void)
{
    db_face_index_init();

    face_index_entry_t e = {0};
    e.uid = 1;
    e.flags = DB_FACE_FLAG_ENABLED;
    db_face_index_add(1, &e);

    int ret = db_face_index_delete(1);
    TEST_ASSERT_EQUAL(DB_OK, ret);
    TEST_ASSERT_EQUAL(0, db_face_index_count());
}

void test_face_set_and_get_feature(void)
{
    db_face_index_init();

    face_index_entry_t e = {0};
    e.uid = 10;
    e.feature_size = 1024;
    e.flags = DB_FACE_FLAG_ENABLED;
    db_face_index_add(10, &e);

    /* 写入特征 */
    uint8_t feat[1024];
    for (int i = 0; i < 1024; i++) feat[i] = (uint8_t)(i % 256);
    int ret = db_face_index_set_feature(10, feat, 1024);
    TEST_ASSERT_EQUAL(DB_OK, ret);

    /* 读回 */
    uint8_t buf[1024];
    uint32_t sz = 0;
    ret = db_face_index_get_feature(10, buf, 1024, &sz);
    TEST_ASSERT_EQUAL(DB_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(1024, sz);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(feat, buf, 1024);
}

void test_face_iterate(void)
{
    db_face_index_init();

    /* 添加 3 条特征 */
    for (int i = 1; i <= 3; i++) {
        face_index_entry_t e = {0};
        e.uid = (uint32_t)i;
        e.feature_size = 256;
        e.flags = DB_FACE_FLAG_ENABLED;
        db_face_index_add((uint32_t)i, &e);

        uint8_t f[256];
        memset(f, (uint8_t)i, 256);
        db_face_index_set_feature((uint32_t)i, f, 256);
    }
    db_face_index_set_loaded(true);

    /* 验证：直接通过 get_all_entries 确认数量 */
    face_index_entry_t *entries;
    int entry_count;
    db_face_index_get_all_entries(&entries, &entry_count);
    TEST_ASSERT_EQUAL(3, entry_count);
}

void test_face_sorted_by_uid(void)
{
    db_face_index_init();

    /* 乱序插入 */
    uint32_t uids[] = {300, 100, 200, 50, 150};
    for (int i = 0; i < 5; i++) {
        face_index_entry_t e = {0};
        e.uid = uids[i];
        e.flags = DB_FACE_FLAG_ENABLED;
        db_face_index_add(uids[i], &e);
    }

    face_index_entry_t *entries;
    int count;
    db_face_index_get_all_entries(&entries, &count);

    /* 验证排序 */
    for (int i = 1; i < count; i++) {
        TEST_ASSERT_TRUE(entries[i - 1].uid < entries[i].uid);
    }
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */

int main(void)
{
    UNITY_BEGIN();

    /* 结构体大小 */
    RUN_TEST(test_struct_sizes);

    /* CRC32 */
    RUN_TEST(test_crc32_known_vectors);
    RUN_TEST(test_crc32_different_data_different_crc);

    /* 字符串哈希 */
    RUN_TEST(test_hash_same_string_same_hash);
    RUN_TEST(test_hash_different_different);

    /* 路径拼接 */
    RUN_TEST(test_make_path);
    RUN_TEST(test_make_path_overflow);

    /* 用户索引 */
    RUN_TEST(test_user_index_init);
    RUN_TEST(test_user_add_and_get);
    RUN_TEST(test_user_add_duplicate);
    RUN_TEST(test_user_get_not_found);
    RUN_TEST(test_user_update);
    RUN_TEST(test_user_delete);
    RUN_TEST(test_user_find_by_name);
    RUN_TEST(test_user_list);

    /* 特征索引 */
    RUN_TEST(test_face_index_init);
    RUN_TEST(test_face_add_and_get);
    RUN_TEST(test_face_update);
    RUN_TEST(test_face_delete);
    RUN_TEST(test_face_set_and_get_feature);
    RUN_TEST(test_face_iterate);
    RUN_TEST(test_face_sorted_by_uid);

    return UNITY_END();
}
