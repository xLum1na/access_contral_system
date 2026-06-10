/**
 * @file    mqtt_client.h
 * @brief   Mock — ESP-MQTT 客户端头文件（宿主机测试用）
 *
 * 提供 service_mqtt.c 编译所需的最小类型定义和函数声明。
 * 函数实现由 test_service_mqtt.c 中的 stub 提供。
 */

#ifndef MOCK_MQTT_CLIENT_H
#define MOCK_MQTT_CLIENT_H

#include "esp_err.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 句柄类型 ---- */
typedef void *esp_mqtt_client_handle_t;
typedef const char *esp_event_base_t;  /**< ESP-IDF 事件基类型 */

/* ---- 错误句柄（简化为不透明指针） ---- */
typedef struct {
    int esp_tls_last_esp_err;
    int esp_tls_stack_err;
    int esp_transport_sock_errno;
} esp_mqtt_error_codes_t;

/* ---- 事件结构体（真实 ESP-MQTT 中为 esp_mqtt_event_t） ---- */
typedef struct {
    int      msg_id;
    char    *topic;
    int      topic_len;
    char    *data;
    int      data_len;
    int      total_data_len;
    int      current_data_offset;
    int      qos;
    int      retain;
    int      dup;
    int      session_present;
    int      error;
    esp_mqtt_error_codes_t *error_handle;
} esp_mqtt_event_static_t;

/** @brief 事件句柄 = 结构体指针（与真实 ESP-MQTT 兼容） */
typedef esp_mqtt_event_static_t *esp_mqtt_event_handle_t;

/* ---- 协议版本 ---- */
#define MQTT_PROTOCOL_V_3_1     0
#define MQTT_PROTOCOL_V_3_1_1   1

/* ---- 事件 ID 枚举 ---- */
typedef enum {
    MQTT_EVENT_ANY              = -1,
    MQTT_EVENT_ERROR            = 1,
    MQTT_EVENT_CONNECTED        = 2,
    MQTT_EVENT_DISCONNECTED     = 3,
    MQTT_EVENT_SUBSCRIBED       = 4,
    MQTT_EVENT_UNSUBSCRIBED     = 5,
    MQTT_EVENT_PUBLISHED        = 6,
    MQTT_EVENT_DATA             = 7,
    MQTT_EVENT_BEFORE_CONNECT   = 8,
    MQTT_EVENT_DELETED          = 9,
} esp_mqtt_event_id_t;

#define ESP_EVENT_ANY_ID        ((int)(-1))

/* ---- 配置结构体（精简版，仅包含 service_mqtt.c 用到的字段） ---- */
typedef struct {
    /* Broker */
    struct {
        struct {
            const char *uri;
            struct {
                const char *certificate;
                const char *client_certificate;
                const char *client_key;
                bool        skip_cert_common_name_check;
            } verification;
        } address;
    } broker;

    /* Credentials */
    struct {
        const char *client_id;
        const char *username;
        struct {
            const char *password;
        } authentication;
    } credentials;

    /* Session */
    struct {
        int  keepalive;
        int  protocol_ver;
        bool disable_clean_session;
        struct {
            const char *topic;
            const char *msg;
            int         msg_len;
            int         qos;
            int         retain;
        } last_will;
    } session;

    /* Network */
    struct {
        int  timeout_ms;
        bool disable_auto_reconnect;
    } network;

    /* Task */
    struct {
        int  priority;
        int  stack_size;
        int  core_id;
    } task;

    /* Buffer */
    struct {
        int  size;
        int  out_size;
    } buffer;
} esp_mqtt_client_config_t;

/* ---- 函数声明 ---- */
esp_mqtt_client_handle_t esp_mqtt_client_init(
    const esp_mqtt_client_config_t *config);

esp_err_t esp_mqtt_client_register_event(
    esp_mqtt_client_handle_t client,
    int event_id,
    void (*event_handler)(void *handler_args, esp_event_base_t base,
                          int32_t event_id, void *event_data),
    void *event_handler_arg);

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);

esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);

int esp_mqtt_client_publish(esp_mqtt_client_handle_t client,
                             const char *topic, const char *data,
                             int len, int qos, int retain);

int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client,
                               const char *topic, int qos);

int esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t client,
                                 const char *topic);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_MQTT_CLIENT_H */
