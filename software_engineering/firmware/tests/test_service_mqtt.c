/**
 * @file    test_service_mqtt.c
 * @brief   service_mqtt 模块单元测试 — 内部逻辑验证
 *
 * 测试覆盖：
 *   - Topic 构造（mqtt_make_topic）
 *   - 命令解析（mqtt_parse_cmd_from_topic）
 *   - 数据结构和配置校验
 *   - 发布事件 JSON 格式
 *   - 心跳 JSON 格式
 *   - 初始化 / 反初始化生命周期
 *   - 状态回调注册
 *   - 自定义 topic handler 注册
 *
 * 注意：MQTT 网络连接依赖 ESP-MQTT 客户端库，
 * 实际通信行为在硬件集成测试中验证。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#include "unity.h"
#include "service_mqtt_internal.h"
#include "mqtt_client.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* ================================================================
 *  测试用常量
 * ================================================================ */

#define TEST_DEVICE_ID  "TEST00000001"
#define TEST_BROKER_URI "mqtts://test-broker.example.com:8883"
#define TEST_CLIENT_ID  "TEST00000001_0001"

/* ================================================================
 *  Mock — OSAL 层（宿主机无 FreeRTOS）
 * ================================================================ */

/* ---- 队列 mock ---- */
static uint8_t  g_mock_queue_buf[4096];
static uint32_t g_mock_queue_head = 0;
static uint32_t g_mock_queue_tail = 0;
static uint32_t g_mock_queue_item_size = 0;
static uint32_t g_mock_queue_max_items = 0;
static uint32_t g_mock_queue_count = 0;

void *osal_queue_create(uint32_t item_size, uint32_t item_count)
{
    g_mock_queue_item_size = item_size;
    g_mock_queue_max_items = item_count;
    g_mock_queue_head      = 0;
    g_mock_queue_tail      = 0;
    g_mock_queue_count     = 0;
    return (void *)0xBEEF0001;
}

bool osal_queue_send(void *queue, const void *item, uint32_t timeout_ms)
{
    (void)queue;
    (void)timeout_ms;

    if (g_mock_queue_count >= g_mock_queue_max_items) return false;

    uint32_t offset = g_mock_queue_tail * g_mock_queue_item_size;
    memcpy(&g_mock_queue_buf[offset], item, g_mock_queue_item_size);

    g_mock_queue_tail = (g_mock_queue_tail + 1) % g_mock_queue_max_items;
    g_mock_queue_count++;
    return true;
}

bool osal_queue_receive(void *queue, void *item, uint32_t timeout_ms)
{
    (void)queue;
    (void)timeout_ms;

    if (g_mock_queue_count == 0) return false;

    uint32_t offset = g_mock_queue_head * g_mock_queue_item_size;
    memcpy(item, &g_mock_queue_buf[offset], g_mock_queue_item_size);

    g_mock_queue_head = (g_mock_queue_head + 1) % g_mock_queue_max_items;
    g_mock_queue_count--;
    return true;
}

uint32_t osal_queue_get_count(void *queue)
{
    (void)queue;
    return g_mock_queue_count;
}

void osal_queue_delete(void *queue)
{
    (void)queue;
    g_mock_queue_head      = 0;
    g_mock_queue_tail      = 0;
    g_mock_queue_count     = 0;
    g_mock_queue_item_size = 0;
    g_mock_queue_max_items = 0;
}

/* ---- 定时器 mock ---- */
static bool g_mock_timer_running = false;

osal_timer_t osal_timer_create_periodic(const char *name, osal_timer_cb_t cb,
                                        void *arg, uint32_t period_ms)
{
    (void)name;
    (void)cb;
    (void)arg;
    (void)period_ms;
    return (void *)0xCAFE0001;
}

osal_timer_t osal_timer_create_one_shot(const char *name, osal_timer_cb_t cb,
                                        void *arg, uint32_t delay_ms)
{
    (void)name;
    (void)cb;
    (void)arg;
    (void)delay_ms;
    return (void *)0xCAFE0002;
}

bool osal_timer_start(void *timer)
{
    (void)timer;
    g_mock_timer_running = true;
    return true;
}

bool osal_timer_stop(void *timer)
{
    (void)timer;
    g_mock_timer_running = false;
    return true;
}

void osal_timer_delete(void *timer)
{
    (void)timer;
    g_mock_timer_running = false;
}

bool osal_timer_is_running(void *timer)
{
    (void)timer;
    return g_mock_timer_running;
}

/* ---- 内存 mock ---- */
void *osal_malloc_caps(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

void *osal_calloc_caps(size_t n, size_t size, uint32_t caps)
{
    (void)caps;
    return calloc(n, size);
}

void *osal_malloc_internal(size_t size) { return malloc(size); }
void *osal_malloc_psram(size_t size)    { return malloc(size); }
void  osal_free(void *ptr)              { free(ptr); }

size_t osal_get_free_heap_size(uint32_t caps)
{
    (void)caps;
    return 250 * 1024 * 1024; /* 250 MB 模拟值 */
}

/* ================================================================
 *  Mock — ESP-MQTT 函数 stub（声明来自 mocks/mqtt_client.h）
 * ================================================================ */

/* ---- ESP-MQTT 函数 stub（声明在 mocks/mqtt_client.h） ---- */
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config)
{
    (void)config;
    return (esp_mqtt_client_handle_t)0xFEED0001;
}

esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client,
                                          int event_id,
                                          void (*event_handler)(void *handler_args,
                                                               esp_event_base_t base,
                                                               int32_t ev_id, void *ev_data),
                                          void *event_handler_arg)
{
    (void)client;
    (void)event_id;
    (void)event_handler;
    (void)event_handler_arg;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}

int esp_mqtt_client_publish(esp_mqtt_client_handle_t client,
                             const char *topic, const char *data,
                             int len, int qos, int retain)
{
    (void)client;
    (void)topic;
    (void)data;
    (void)len;
    (void)qos;
    (void)retain;
    return 100; /* 模拟 msg_id */
}

int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client,
                               const char *topic, int qos)
{
    (void)client;
    (void)topic;
    (void)qos;
    return 200;
}

int esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t client,
                                 const char *topic)
{
    (void)client;
    (void)topic;
    return 300;
}

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */

void setUp(void)
{
    /* 重置全局状态 */
    memset(&g_mqtt_ctx, 0, sizeof(g_mqtt_ctx));

    /* 重置 mock */
    memset(g_mock_queue_buf, 0, sizeof(g_mock_queue_buf));
    g_mock_queue_head       = 0;
    g_mock_queue_tail       = 0;
    g_mock_queue_item_size  = 0;
    g_mock_queue_max_items  = 0;
    g_mock_queue_count      = 0;
    g_mock_timer_running    = false;
}

void tearDown(void)
{
    /* 清理：若 init 过则 deinit */
    if (g_mqtt_ctx.inited) {
        service_mqtt_deinit();
    }
    memset(&g_mqtt_ctx, 0, sizeof(g_mqtt_ctx));
}

/* ================================================================
 *  测试：数据结构大小
 * ================================================================ */

void test_struct_sizes(void)
{
    /* mqtt_publish_req_t 应小于 2.5KB（topic + payload） */
    TEST_ASSERT_TRUE(sizeof(mqtt_publish_req_t) <= 2560);

    /* mqtt_context_t 不应过大 */
    TEST_ASSERT_TRUE(sizeof(mqtt_context_t) < 4096);

    /* mqtt_topic_handler_t */
    TEST_ASSERT_TRUE(sizeof(mqtt_topic_handler_t) < 256);
}

/* ================================================================
 *  测试：Topic 构造
 * ================================================================ */

void test_make_topic_event(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = TEST_DEVICE_ID,
        .broker_uri  = TEST_BROKER_URI,
        .client_id   = TEST_CLIENT_ID,
    };
    service_mqtt_init(&cfg);

    const char *topic = mqtt_make_topic(MQTT_TOPIC_EVENT);
    TEST_ASSERT_NOT_NULL(topic);
    TEST_ASSERT_EQUAL_STRING("face/TEST00000001/event", topic);
}

void test_make_topic_heartbeat(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "SN12345678",
        .broker_uri  = TEST_BROKER_URI,
        .client_id   = TEST_CLIENT_ID,
    };
    service_mqtt_init(&cfg);

    const char *topic = mqtt_make_topic(MQTT_TOPIC_HEARTBEAT);
    TEST_ASSERT_EQUAL_STRING("face/SN12345678/heartbeat", topic);
}

void test_make_topic_cmd(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "DEV_X",
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    const char *topic = mqtt_make_topic(MQTT_TOPIC_CMD);
    TEST_ASSERT_EQUAL_STRING("face/DEV_X/cmd", topic);
}

void test_make_topic_lwt(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "UNIT001",
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    const char *topic = mqtt_make_topic(MQTT_TOPIC_LWT);
    TEST_ASSERT_EQUAL_STRING("face/UNIT001/status", topic);
}

/* ================================================================
 *  测试：命令解析
 * ================================================================ */

void test_parse_cmd_from_topic_user_add(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "DEV01",
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    const char *topic = "face/DEV01/user/add";
    service_mqtt_cmd_t cmd = mqtt_parse_cmd_from_topic(topic);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_CMD_USER_ADD, cmd);
}

void test_parse_cmd_from_topic_user_del(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "DEV01",
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    const char *topic = "face/DEV01/user/del";
    service_mqtt_cmd_t cmd = mqtt_parse_cmd_from_topic(topic);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_CMD_USER_DEL, cmd);
}

void test_parse_cmd_from_topic_user_update(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "GW_99",
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    const char *topic = "face/GW_99/user/update";
    service_mqtt_cmd_t cmd = mqtt_parse_cmd_from_topic(topic);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_CMD_USER_UPDATE, cmd);
}

void test_parse_cmd_from_topic_config(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "NODE_A",
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    const char *topic = "face/NODE_A/config";
    service_mqtt_cmd_t cmd = mqtt_parse_cmd_from_topic(topic);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_CMD_CONFIG_SET, cmd);
}

void test_parse_cmd_from_topic_ota(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "DEV_OTA",
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    const char *topic = "face/DEV_OTA/ota";
    service_mqtt_cmd_t cmd = mqtt_parse_cmd_from_topic(topic);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_CMD_OTA_START, cmd);
}

void test_parse_cmd_from_topic_unknown(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "X1",
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    /* 未知 topic */
    const char *topic = "face/X1/unknown_command";
    service_mqtt_cmd_t cmd = mqtt_parse_cmd_from_topic(topic);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_CMD_NONE, cmd);
}

void test_parse_cmd_different_device_id(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "DEV_A",
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    /* 发给其他设备的 topic，不应被解析为已知命令 */
    const char *topic = "face/DEV_B/user/add";
    service_mqtt_cmd_t cmd = mqtt_parse_cmd_from_topic(topic);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_CMD_NONE, cmd);
}

/* ================================================================
 *  测试：初始化 / 反初始化
 * ================================================================ */

void test_init_success(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = TEST_DEVICE_ID,
        .broker_uri  = TEST_BROKER_URI,
        .client_id   = TEST_CLIENT_ID,
    };

    int ret = service_mqtt_init(&cfg);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);
    TEST_ASSERT_TRUE(g_mqtt_ctx.inited);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_STATE_DISCONNECTED, g_mqtt_ctx.state);
    TEST_ASSERT_NOT_NULL(g_mqtt_ctx.publish_queue);
}

void test_init_null_cfg(void)
{
    int ret = service_mqtt_init(NULL);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_PARAM, ret);
}

void test_init_empty_broker(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = "",
    };

    int ret = service_mqtt_init(&cfg);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_PARAM, ret);
}

void test_init_double(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };

    int ret1 = service_mqtt_init(&cfg);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret1);

    int ret2 = service_mqtt_init(&cfg);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_ALREADY_INIT, ret2);
}

void test_init_defaults(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
        /* 其他字段全部为 0 */
    };

    int ret = service_mqtt_init(&cfg);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);

    /* 默认值应被补全 */
    TEST_ASSERT_EQUAL(MQTT_RECONNECT_MIN_MS / 1000, g_mqtt_ctx.cfg.reconnect_min_s);
    TEST_ASSERT_EQUAL(MQTT_RECONNECT_MAX_MS / 1000, g_mqtt_ctx.cfg.reconnect_max_s);
    TEST_ASSERT_EQUAL(MQTT_KEEP_ALIVE_DEFAULT_S, g_mqtt_ctx.cfg.keep_alive_s);
}

void test_deinit(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    int ret = service_mqtt_deinit();
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);
    TEST_ASSERT_FALSE(g_mqtt_ctx.inited);
}

void test_deinit_not_inited(void)
{
    int ret = service_mqtt_deinit();
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_NOT_INIT, ret);
}

/* ================================================================
 *  测试：启动 / 停止
 * ================================================================ */

void test_start_not_inited(void)
{
    int ret = service_mqtt_start();
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_NOT_INIT, ret);
}

void test_stop_not_inited(void)
{
    int ret = service_mqtt_stop();
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_NOT_INIT, ret);
}

void test_start_and_stop(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    /* start → 进入 CONNECTING 状态 */
    int ret = service_mqtt_start();
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_STATE_CONNECTING, g_mqtt_ctx.state);

    /* stop → 回 DISCONNECTED */
    ret = service_mqtt_stop();
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_STATE_DISCONNECTED, g_mqtt_ctx.state);
}

void test_stop_without_start(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    /* 未 start 直接 stop 应 OK（幂等） */
    int ret = service_mqtt_stop();
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);
}

/* ================================================================
 *  测试：状态查询
 * ================================================================ */

void test_get_state_initial(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    TEST_ASSERT_EQUAL(SERVICE_MQTT_STATE_DISCONNECTED,
                      service_mqtt_get_state());
    TEST_ASSERT_FALSE(service_mqtt_is_connected());
}

/* ================================================================
 *  测试：回调注册
 * ================================================================ */

static int g_test_state_cb_called = 0;
static service_mqtt_state_t g_test_last_state = SERVICE_MQTT_STATE_DISCONNECTED;

static void test_state_cb(service_mqtt_state_t state, void *arg)
{
    g_test_state_cb_called++;
    g_test_last_state = state;
    (void)arg;
}

void test_state_callback_registration(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    service_mqtt_set_state_callback(test_state_cb, NULL);
    TEST_ASSERT_EQUAL_PTR((void *)test_state_cb,
                          (void *)g_mqtt_ctx.state_cb);

    /* 手动触发状态切换 */
    mqtt_set_state(SERVICE_MQTT_STATE_CONNECTING);
    TEST_ASSERT_EQUAL(1, g_test_state_cb_called);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_STATE_CONNECTING, g_test_last_state);
}

/* ================================================================
 *  测试：Topic handler 注册
 * ================================================================ */

static int g_test_handler_called = 0;

static void test_handler_cb(service_mqtt_cmd_t cmd,
                             const service_mqtt_message_t *msg, void *arg)
{
    g_test_handler_called++;
    (void)cmd;
    (void)msg;
    (void)arg;
}

void test_register_topic_handler(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    int ret = service_mqtt_register_topic_handler(
        "face/+/custom", test_handler_cb, NULL);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);
    TEST_ASSERT_EQUAL(1, g_mqtt_ctx.topic_handler_count);
}

void test_register_topic_handler_null_cb(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    int ret = service_mqtt_register_topic_handler("test/#", NULL, NULL);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_PARAM, ret);
}

void test_register_topic_handler_max(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    /* 填满 handler 槽位 */
    for (int i = 0; i < MQTT_MAX_TOPIC_HANDLERS; i++) {
        char filter[64];
        snprintf(filter, sizeof(filter), "test/%d", i);
        int ret = service_mqtt_register_topic_handler(
            filter, test_handler_cb, NULL);
        TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);
    }

    /* 第 9 个应失败 */
    int ret = service_mqtt_register_topic_handler(
        "overflow", test_handler_cb, NULL);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_NO_MEM, ret);
}

/* ================================================================
 *  测试：重连逻辑
 * ================================================================ */

void test_reconnect_exponential_backoff(void)
{
    service_mqtt_config_t cfg = {
        .device_id        = TEST_DEVICE_ID,
        .broker_uri       = TEST_BROKER_URI,
        .reconnect_min_s  = 1,
        .reconnect_max_s  = 32,
    };
    service_mqtt_init(&cfg);

    /* 初始间隔 = min = 1s → 1000ms */
    mqtt_reset_reconnect();
    uint32_t d1 = mqtt_next_reconnect_delay();
    TEST_ASSERT_EQUAL_UINT32(1000, d1);

    uint32_t expected = 2000;
    for (int i = 0; i < 5; i++) {
        uint32_t d = mqtt_next_reconnect_delay();
        if (d < g_mqtt_ctx.cfg.reconnect_max_s * 1000) {
            TEST_ASSERT_EQUAL_UINT32(expected, d);
            expected *= 2;
        }
    }

    /* 重置后回到初始值 */
    mqtt_reset_reconnect();
    uint32_t d_reset = mqtt_next_reconnect_delay();
    TEST_ASSERT_EQUAL_UINT32(1000, d_reset);
}

void test_reconnect_respects_max(void)
{
    service_mqtt_config_t cfg = {
        .device_id        = TEST_DEVICE_ID,
        .broker_uri       = TEST_BROKER_URI,
        .reconnect_min_s  = 1,
        .reconnect_max_s  = 8,
    };
    service_mqtt_init(&cfg);

    mqtt_reset_reconnect();

    /* 连续取多次，最大值不超过 8000ms */
    for (int i = 0; i < 10; i++) {
        uint32_t d = mqtt_next_reconnect_delay();
        TEST_ASSERT_TRUE(d <= 8000);
    }
}

/* ================================================================
 *  测试：状态切换
 * ================================================================ */

void test_set_state_duplicate_ignored(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    g_test_state_cb_called = 0;
    service_mqtt_set_state_callback(test_state_cb, NULL);

    /* 首次切换 */
    mqtt_set_state(SERVICE_MQTT_STATE_CONNECTING);
    TEST_ASSERT_EQUAL(1, g_test_state_cb_called);

    /* 重复设置同状态，不触发回调 */
    mqtt_set_state(SERVICE_MQTT_STATE_CONNECTING);
    TEST_ASSERT_EQUAL(1, g_test_state_cb_called);

    /* 不同状态，触发 */
    mqtt_set_state(SERVICE_MQTT_STATE_CONNECTED);
    TEST_ASSERT_EQUAL(2, g_test_state_cb_called);
}

/* ================================================================
 *  测试：发布事件 JSON 格式
 * ================================================================ */

void test_publish_event_json_format(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = TEST_DEVICE_ID,
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    /* 保持 DISCONNECTED 状态，publish 走队列路径 */
    int ret = service_mqtt_publish_event(SERVICE_MQTT_EVENT_PASS, 1001, 95, 42);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);

    /* 从 mock 队列取出验证 payload */
    mqtt_publish_req_t req;
    bool ok = osal_queue_receive(g_mqtt_ctx.publish_queue, &req, 0);
    TEST_ASSERT_TRUE(ok);

    /* 验证 topic */
    TEST_ASSERT_EQUAL_STRING("face/TEST00000001/event", req.topic);

    /* 验证 JSON 关键字段 */
    const char *payload = (const char *)req.payload;
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"event\":0"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"uid\":1001"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"score\":95"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"image_id\":42"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"ts\":"));

    /* QOS 应为 1（事件消息重要） */
    TEST_ASSERT_EQUAL(1, req.qos);
    TEST_ASSERT_FALSE(req.retain);
}

void test_publish_event_duress(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = "SEC01",
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    /* 走队列路径 */
    int ret = service_mqtt_publish_event(SERVICE_MQTT_EVENT_DURESS, 999, 88, 0);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);

    mqtt_publish_req_t req;
    bool ok = osal_queue_receive(g_mqtt_ctx.publish_queue, &req, 0);
    TEST_ASSERT_TRUE(ok);

    const char *payload = (const char *)req.payload;
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"event\":2"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"uid\":999"));
}

/* ================================================================
 *  测试：心跳 JSON 格式
 * ================================================================ */

void test_publish_heartbeat_json_format(void)
{
    service_mqtt_config_t cfg = {
        .device_id   = TEST_DEVICE_ID,
        .broker_uri  = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    /* 走队列路径 */
    int ret = service_mqtt_publish_heartbeat();
    TEST_ASSERT_EQUAL(SERVICE_MQTT_OK, ret);

    mqtt_publish_req_t req;
    bool ok = osal_queue_receive(g_mqtt_ctx.publish_queue, &req, 0);
    TEST_ASSERT_TRUE(ok);

    const char *payload = (const char *)req.payload;
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"ts\":"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"free_heap\":"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"uptime\":"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"pub_ok\":"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"pub_fail\":"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"msg_recv\":"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"conn_count\":"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"disconn_count\":"));

    /* topic 应正确 */
    TEST_ASSERT_EQUAL_STRING("face/TEST00000001/heartbeat", req.topic);

    /* 心跳 QOS=0 */
    TEST_ASSERT_EQUAL(0, req.qos);
}

/* ================================================================
 *  测试：发布未初始化
 * ================================================================ */

void test_publish_not_inited(void)
{
    int ret = service_mqtt_publish("test/topic",
                                   (const uint8_t *)"hello", 5, 0, false);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_NOT_INIT, ret);
}

/* ================================================================
 *  测试：发布参数校验
 * ================================================================ */

void test_publish_null_topic(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    int ret = service_mqtt_publish(NULL, (const uint8_t *)"x", 1, 0, false);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_PARAM, ret);
}

void test_publish_null_payload(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    int ret = service_mqtt_publish("test", NULL, 5, 0, false);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_PARAM, ret);
}

void test_publish_invalid_qos(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    int ret = service_mqtt_publish("test", (const uint8_t *)"x", 1, 3, false);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_PARAM, ret);
}

/* ================================================================
 *  测试：subscribe 未连接
 * ================================================================ */

void test_subscribe_not_connected(void)
{
    service_mqtt_config_t cfg = {
        .device_id  = TEST_DEVICE_ID,
        .broker_uri = TEST_BROKER_URI,
    };
    service_mqtt_init(&cfg);

    /* 未 start/未连接时 subscribe 应返回错误 */
    int ret = service_mqtt_subscribe("face/test", 0);
    TEST_ASSERT_EQUAL(SERVICE_MQTT_ERR_NOT_CONNECTED, ret);
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */

int main(void)
{
    UNITY_BEGIN();

    /* 结构体大小 */
    RUN_TEST(test_struct_sizes);

    /* Topic 构造 */
    RUN_TEST(test_make_topic_event);
    RUN_TEST(test_make_topic_heartbeat);
    RUN_TEST(test_make_topic_cmd);
    RUN_TEST(test_make_topic_lwt);

    /* 命令解析 */
    RUN_TEST(test_parse_cmd_from_topic_user_add);
    RUN_TEST(test_parse_cmd_from_topic_user_del);
    RUN_TEST(test_parse_cmd_from_topic_user_update);
    RUN_TEST(test_parse_cmd_from_topic_config);
    RUN_TEST(test_parse_cmd_from_topic_ota);
    RUN_TEST(test_parse_cmd_from_topic_unknown);
    RUN_TEST(test_parse_cmd_different_device_id);

    /* 初始化 / 反初始化 */
    RUN_TEST(test_init_success);
    RUN_TEST(test_init_null_cfg);
    RUN_TEST(test_init_empty_broker);
    RUN_TEST(test_init_double);
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_deinit);
    RUN_TEST(test_deinit_not_inited);

    /* 启动 / 停止 */
    RUN_TEST(test_start_not_inited);
    RUN_TEST(test_stop_not_inited);
    RUN_TEST(test_start_and_stop);
    RUN_TEST(test_stop_without_start);

    /* 状态查询 */
    RUN_TEST(test_get_state_initial);

    /* 回调注册 */
    RUN_TEST(test_state_callback_registration);

    /* Topic handler 注册 */
    RUN_TEST(test_register_topic_handler);
    RUN_TEST(test_register_topic_handler_null_cb);
    RUN_TEST(test_register_topic_handler_max);

    /* 重连逻辑 */
    RUN_TEST(test_reconnect_exponential_backoff);
    RUN_TEST(test_reconnect_respects_max);

    /* 状态切换 */
    RUN_TEST(test_set_state_duplicate_ignored);

    /* 发布事件 JSON */
    RUN_TEST(test_publish_event_json_format);
    RUN_TEST(test_publish_event_duress);

    /* 心跳 JSON */
    RUN_TEST(test_publish_heartbeat_json_format);

    /* 错误场景 */
    RUN_TEST(test_publish_not_inited);
    RUN_TEST(test_publish_null_topic);
    RUN_TEST(test_publish_null_payload);
    RUN_TEST(test_publish_invalid_qos);
    RUN_TEST(test_subscribe_not_connected);

    return UNITY_END();
}
