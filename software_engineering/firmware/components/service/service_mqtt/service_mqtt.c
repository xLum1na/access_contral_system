/**
 * @file    service_mqtt.c
 * @brief   MQTT 服务实现
 */

#include "service_mqtt_internal.h"

#include "osal_memory.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

mqtt_context_t g_mqtt_ctx;
static char s_topic_buf[MQTT_MAX_TOPIC_LEN];

static const char *safe_client_id(void)
{
    return (g_mqtt_ctx.cfg.client_id && g_mqtt_ctx.cfg.client_id[0]) ?
           g_mqtt_ctx.cfg.client_id : g_mqtt_ctx.cfg.device_id;
}

static bool valid_qos(int qos)
{
    return qos >= 0 && qos <= 2;
}

static void fill_defaults(void)
{
    if (g_mqtt_ctx.cfg.reconnect_min_s == 0) {
        g_mqtt_ctx.cfg.reconnect_min_s = MQTT_RECONNECT_MIN_MS / 1000;
    }
    if (g_mqtt_ctx.cfg.reconnect_max_s == 0) {
        g_mqtt_ctx.cfg.reconnect_max_s = MQTT_RECONNECT_MAX_MS / 1000;
    }
    if (g_mqtt_ctx.cfg.keep_alive_s == 0) {
        g_mqtt_ctx.cfg.keep_alive_s = MQTT_KEEP_ALIVE_DEFAULT_S;
    }
}

void mqtt_set_state(service_mqtt_state_t state)
{
    if (g_mqtt_ctx.state == state) return;
    g_mqtt_ctx.state = state;
    if (state == SERVICE_MQTT_STATE_CONNECTED) {
        g_mqtt_ctx.conn_count++;
    } else if (state == SERVICE_MQTT_STATE_DISCONNECTED) {
        g_mqtt_ctx.disconn_count++;
    }
    if (g_mqtt_ctx.state_cb) {
        g_mqtt_ctx.state_cb(state, g_mqtt_ctx.state_cb_arg);
    }
}

const char *mqtt_make_topic(mqtt_topic_type_t type)
{
    const char *suffix = NULL;

    if (!g_mqtt_ctx.inited || !g_mqtt_ctx.cfg.device_id) return NULL;
    switch (type) {
    case MQTT_TOPIC_EVENT:     suffix = "event"; break;
    case MQTT_TOPIC_HEARTBEAT: suffix = "heartbeat"; break;
    case MQTT_TOPIC_CMD:       suffix = "cmd"; break;
    case MQTT_TOPIC_LWT:       suffix = "status"; break;
    case MQTT_TOPIC_STATUS:    suffix = "status"; break;
    case MQTT_TOPIC_ACCESS:    suffix = "access"; break;
    case MQTT_TOPIC_ALARM:     suffix = "alarm"; break;
    case MQTT_TOPIC_USER_SET:  suffix = "user/set"; break;
    case MQTT_TOPIC_REPLY:     suffix = "reply"; break;
    default: return NULL;
    }
    (void)snprintf(s_topic_buf, sizeof(s_topic_buf), "face/%s/%s",
                   g_mqtt_ctx.cfg.device_id, suffix);
    return s_topic_buf;
}

service_mqtt_cmd_t mqtt_parse_cmd_from_topic(const char *topic)
{
    char prefix[80];
    const char *cmd;

    if (!g_mqtt_ctx.inited || !topic || !g_mqtt_ctx.cfg.device_id) return SERVICE_MQTT_CMD_NONE;
    (void)snprintf(prefix, sizeof(prefix), "face/%s/", g_mqtt_ctx.cfg.device_id);
    if (strncmp(topic, prefix, strlen(prefix)) != 0) return SERVICE_MQTT_CMD_NONE;
    cmd = topic + strlen(prefix);
    if (strcmp(cmd, "user/add") == 0) return SERVICE_MQTT_CMD_USER_ADD;
    if (strcmp(cmd, "user/del") == 0) return SERVICE_MQTT_CMD_USER_DEL;
    if (strcmp(cmd, "user/update") == 0) return SERVICE_MQTT_CMD_USER_UPDATE;
    if (strcmp(cmd, "user/set") == 0) return SERVICE_MQTT_CMD_USER_SET;
    if (strcmp(cmd, "config") == 0) return SERVICE_MQTT_CMD_CONFIG_SET;
    if (strcmp(cmd, "ota") == 0) return SERVICE_MQTT_CMD_OTA_START;
    if (strcmp(cmd, "cmd") == 0) return SERVICE_MQTT_CMD_REBOOT;
    return SERVICE_MQTT_CMD_NONE;
}

void mqtt_reset_reconnect(void)
{
    g_mqtt_ctx.reconnect_delay_ms = 0;
}

uint32_t mqtt_next_reconnect_delay(void)
{
    uint32_t min_ms = g_mqtt_ctx.cfg.reconnect_min_s * 1000U;
    uint32_t max_ms = g_mqtt_ctx.cfg.reconnect_max_s * 1000U;

    if (min_ms == 0) min_ms = MQTT_RECONNECT_MIN_MS;
    if (max_ms == 0) max_ms = MQTT_RECONNECT_MAX_MS;
    if (g_mqtt_ctx.reconnect_delay_ms == 0) {
        g_mqtt_ctx.reconnect_delay_ms = min_ms;
    } else if (g_mqtt_ctx.reconnect_delay_ms < max_ms) {
        g_mqtt_ctx.reconnect_delay_ms *= 2U;
        if (g_mqtt_ctx.reconnect_delay_ms > max_ms) g_mqtt_ctx.reconnect_delay_ms = max_ms;
    }
    return g_mqtt_ctx.reconnect_delay_ms;
}

int service_mqtt_init(const service_mqtt_config_t *cfg)
{
    esp_mqtt_client_config_t mqtt_cfg;

    if (!cfg || !cfg->device_id || !cfg->broker_uri || cfg->device_id[0] == '\0' ||
        cfg->broker_uri[0] == '\0') {
        return SERVICE_MQTT_ERR_PARAM;
    }
    if (g_mqtt_ctx.inited) return SERVICE_MQTT_ERR_ALREADY_INIT;

    memset(&g_mqtt_ctx, 0, sizeof(g_mqtt_ctx));
    g_mqtt_ctx.cfg = *cfg;
    fill_defaults();
    g_mqtt_ctx.publish_queue = osal_queue_create(sizeof(mqtt_publish_req_t), MQTT_PUBLISH_QUEUE_LEN);
    if (!g_mqtt_ctx.publish_queue) return SERVICE_MQTT_ERR_NO_MEM;

    memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
    mqtt_cfg.broker.address.uri = cfg->broker_uri;
    mqtt_cfg.credentials.client_id = safe_client_id();
    mqtt_cfg.credentials.username = cfg->username;
    mqtt_cfg.credentials.authentication.password = cfg->password;
    mqtt_cfg.session.keepalive = (int)g_mqtt_ctx.cfg.keep_alive_s;
    mqtt_cfg.session.last_will.topic = mqtt_make_topic(MQTT_TOPIC_LWT);
    mqtt_cfg.session.last_will.msg = "offline";
    mqtt_cfg.session.last_will.msg_len = 7;
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.last_will.retain = 1;
    g_mqtt_ctx.client = esp_mqtt_client_init(&mqtt_cfg);
    if (!g_mqtt_ctx.client) {
        osal_queue_delete(g_mqtt_ctx.publish_queue);
        memset(&g_mqtt_ctx, 0, sizeof(g_mqtt_ctx));
        return SERVICE_MQTT_ERR_NO_MEM;
    }
    (void)esp_mqtt_client_register_event(g_mqtt_ctx.client, ESP_EVENT_ANY_ID, NULL, NULL);
    g_mqtt_ctx.state = SERVICE_MQTT_STATE_DISCONNECTED;
    g_mqtt_ctx.inited = true;
    return SERVICE_MQTT_OK;
}

int service_mqtt_deinit(void)
{
    if (!g_mqtt_ctx.inited) return SERVICE_MQTT_ERR_NOT_INIT;
    if (g_mqtt_ctx.client) {
        (void)esp_mqtt_client_destroy(g_mqtt_ctx.client);
    }
    if (g_mqtt_ctx.publish_queue) {
        osal_queue_delete(g_mqtt_ctx.publish_queue);
    }
    memset(&g_mqtt_ctx, 0, sizeof(g_mqtt_ctx));
    return SERVICE_MQTT_OK;
}

int service_mqtt_start(void)
{
    if (!g_mqtt_ctx.inited) return SERVICE_MQTT_ERR_NOT_INIT;
    if (esp_mqtt_client_start(g_mqtt_ctx.client) != ESP_OK) return SERVICE_MQTT_ERR_OFFLINE;
    mqtt_set_state(SERVICE_MQTT_STATE_CONNECTING);
    return SERVICE_MQTT_OK;
}

int service_mqtt_stop(void)
{
    if (!g_mqtt_ctx.inited) return SERVICE_MQTT_ERR_NOT_INIT;
    if (g_mqtt_ctx.client) (void)esp_mqtt_client_stop(g_mqtt_ctx.client);
    mqtt_set_state(SERVICE_MQTT_STATE_DISCONNECTED);
    return SERVICE_MQTT_OK;
}

int service_mqtt_publish(const char *topic, const uint8_t *payload,
                         uint32_t payload_len, int qos, bool retain)
{
    mqtt_publish_req_t req;

    if (!g_mqtt_ctx.inited) return SERVICE_MQTT_ERR_NOT_INIT;
    if (!topic || !payload || payload_len == 0 || payload_len >= MQTT_MAX_PAYLOAD_LEN || !valid_qos(qos)) {
        return SERVICE_MQTT_ERR_PARAM;
    }

    memset(&req, 0, sizeof(req));
    strncpy(req.topic, topic, sizeof(req.topic) - 1);
    memcpy(req.payload, payload, payload_len);
    req.payload[payload_len] = '\0';
    req.payload_len = payload_len;
    req.qos = qos;
    req.retain = retain;

    if (g_mqtt_ctx.state == SERVICE_MQTT_STATE_CONNECTED) {
        int msg_id = esp_mqtt_client_publish(g_mqtt_ctx.client, req.topic,
                                             (const char *)req.payload,
                                             (int)req.payload_len, qos, retain);
        if (msg_id < 0) {
            g_mqtt_ctx.pub_fail++;
            return SERVICE_MQTT_ERR_OFFLINE;
        }
        g_mqtt_ctx.pub_ok++;
        return SERVICE_MQTT_OK;
    }

    if (!osal_queue_send(g_mqtt_ctx.publish_queue, &req, 0)) {
        g_mqtt_ctx.pub_fail++;
        return SERVICE_MQTT_ERR_NO_MEM;
    }
    return SERVICE_MQTT_OK;
}

int service_mqtt_publish_event(service_mqtt_event_type_t event, uint32_t uid,
                               uint8_t score, uint32_t image_id)
{
    char payload[256];
    const char *topic;
    int len;

    topic = mqtt_make_topic(MQTT_TOPIC_EVENT);
    if (!topic) return SERVICE_MQTT_ERR_NOT_INIT;
    len = snprintf(payload, sizeof(payload),
                   "{\"event\":%d,\"uid\":%lu,\"score\":%u,\"image_id\":%lu,\"ts\":%lu}",
                   (int)event, (unsigned long)uid, (unsigned)score,
                   (unsigned long)image_id, (unsigned long)time(NULL));
    if (len <= 0 || len >= (int)sizeof(payload)) return SERVICE_MQTT_ERR_PARAM;
    return service_mqtt_publish(topic, (const uint8_t *)payload, (uint32_t)len, 1, false);
}

int service_mqtt_publish_status(const service_mqtt_status_payload_t *status)
{
    char payload[192];
    const char *topic;
    int len;

    if (!status) return SERVICE_MQTT_ERR_PARAM;
    topic = mqtt_make_topic(MQTT_TOPIC_STATUS);
    if (!topic) return SERVICE_MQTT_ERR_NOT_INIT;
    len = snprintf(payload, sizeof(payload),
                   "{\"uptime\":%lu,\"free_heap\":%lu,\"ts\":%lu}",
                   (unsigned long)status->uptime, (unsigned long)status->free_heap,
                   (unsigned long)time(NULL));
    if (len <= 0 || len >= (int)sizeof(payload)) return SERVICE_MQTT_ERR_PARAM;
    return service_mqtt_publish(topic, (const uint8_t *)payload, (uint32_t)len, 0, false);
}

int service_mqtt_publish_heartbeat(void)
{
    char payload[256];
    const char *topic;
    int len;

    topic = mqtt_make_topic(MQTT_TOPIC_HEARTBEAT);
    if (!topic) return SERVICE_MQTT_ERR_NOT_INIT;
    len = snprintf(payload, sizeof(payload),
                   "{\"ts\":%lu,\"free_heap\":%lu,\"uptime\":0,\"pub_ok\":%lu,\"pub_fail\":%lu,\"msg_recv\":%lu,\"conn_count\":%lu,\"disconn_count\":%lu}",
                   (unsigned long)time(NULL), (unsigned long)osal_get_free_heap_size(0),
                   (unsigned long)g_mqtt_ctx.pub_ok, (unsigned long)g_mqtt_ctx.pub_fail,
                   (unsigned long)g_mqtt_ctx.msg_recv, (unsigned long)g_mqtt_ctx.conn_count,
                   (unsigned long)g_mqtt_ctx.disconn_count);
    if (len <= 0 || len >= (int)sizeof(payload)) return SERVICE_MQTT_ERR_PARAM;
    return service_mqtt_publish(topic, (const uint8_t *)payload, (uint32_t)len, 0, false);
}

int service_mqtt_get_status(service_mqtt_status_t *status)
{
    if (!g_mqtt_ctx.inited) return SERVICE_MQTT_ERR_NOT_INIT;
    if (!status) return SERVICE_MQTT_ERR_PARAM;
    memset(status, 0, sizeof(*status));
    status->state = g_mqtt_ctx.state;
    status->inited = g_mqtt_ctx.inited;
    status->connected = service_mqtt_is_connected();
    status->pub_ok = g_mqtt_ctx.pub_ok;
    status->pub_fail = g_mqtt_ctx.pub_fail;
    status->msg_recv = g_mqtt_ctx.msg_recv;
    status->conn_count = g_mqtt_ctx.conn_count;
    status->disconn_count = g_mqtt_ctx.disconn_count;
    return SERVICE_MQTT_OK;
}

service_mqtt_state_t service_mqtt_get_state(void)
{
    return g_mqtt_ctx.state;
}

bool service_mqtt_is_connected(void)
{
    return g_mqtt_ctx.inited && g_mqtt_ctx.state == SERVICE_MQTT_STATE_CONNECTED;
}

int service_mqtt_set_state_callback(service_mqtt_state_cb_t cb, void *arg)
{
    if (!g_mqtt_ctx.inited) return SERVICE_MQTT_ERR_NOT_INIT;
    g_mqtt_ctx.state_cb = cb;
    g_mqtt_ctx.state_cb_arg = arg;
    return SERVICE_MQTT_OK;
}

int service_mqtt_register_topic_handler(const char *topic_filter,
                                        service_mqtt_topic_handler_t handler,
                                        void *arg)
{
    mqtt_topic_handler_t *slot;

    if (!g_mqtt_ctx.inited) return SERVICE_MQTT_ERR_NOT_INIT;
    if (!topic_filter || !handler) return SERVICE_MQTT_ERR_PARAM;
    if (g_mqtt_ctx.topic_handler_count >= MQTT_MAX_TOPIC_HANDLERS) return SERVICE_MQTT_ERR_NO_MEM;
    slot = &g_mqtt_ctx.topic_handlers[g_mqtt_ctx.topic_handler_count++];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->topic_filter, topic_filter, sizeof(slot->topic_filter) - 1);
    slot->handler = handler;
    slot->arg = arg;
    return SERVICE_MQTT_OK;
}

int service_mqtt_subscribe(const char *topic, int qos)
{
    if (!g_mqtt_ctx.inited) return SERVICE_MQTT_ERR_NOT_INIT;
    if (!topic || !valid_qos(qos)) return SERVICE_MQTT_ERR_PARAM;
    if (!service_mqtt_is_connected()) return SERVICE_MQTT_ERR_NOT_CONNECTED;
    return esp_mqtt_client_subscribe(g_mqtt_ctx.client, topic, qos) >= 0 ?
           SERVICE_MQTT_OK : SERVICE_MQTT_ERR_OFFLINE;
}

int service_mqtt_handle_remote_command(const service_mqtt_message_t *msg)
{
    if (!g_mqtt_ctx.inited) return SERVICE_MQTT_ERR_NOT_INIT;
    if (!msg) return SERVICE_MQTT_ERR_PARAM;
    return SERVICE_MQTT_ERR_AUTH_REQUIRED;
}
