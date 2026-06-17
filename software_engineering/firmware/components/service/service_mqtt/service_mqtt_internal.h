/**
 * @file    service_mqtt_internal.h
 * @brief   MQTT 服务内部定义（宿主机测试可见）
 */

#ifndef SERVICE_MQTT_INTERNAL_H
#define SERVICE_MQTT_INTERNAL_H

#include "service_mqtt.h"
#include "mqtt_client.h"
#include "osal_queue.h"
#include "osal_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char topic_filter[MQTT_MAX_TOPIC_LEN];
    service_mqtt_topic_handler_t handler;
    void *arg;
} mqtt_topic_handler_t;

typedef struct {
    bool inited;
    service_mqtt_config_t cfg;
    service_mqtt_state_t state;
    esp_mqtt_client_handle_t client;
    osal_queue_t publish_queue;
    osal_timer_t reconnect_timer;
    service_mqtt_state_cb_t state_cb;
    void *state_cb_arg;
    mqtt_topic_handler_t topic_handlers[MQTT_MAX_TOPIC_HANDLERS];
    uint32_t topic_handler_count;
    uint32_t reconnect_delay_ms;
    uint32_t pub_ok;
    uint32_t pub_fail;
    uint32_t msg_recv;
    uint32_t conn_count;
    uint32_t disconn_count;
} mqtt_context_t;

extern mqtt_context_t g_mqtt_ctx;

const char *mqtt_make_topic(mqtt_topic_type_t type);
service_mqtt_cmd_t mqtt_parse_cmd_from_topic(const char *topic);
void mqtt_set_state(service_mqtt_state_t state);
void mqtt_reset_reconnect(void);
uint32_t mqtt_next_reconnect_delay(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_MQTT_INTERNAL_H */
