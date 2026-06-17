/**
 * @file    service_mqtt.h
 * @brief   MQTT 服务 — 云端连接与事件上报安全骨架
 *
 * 当前实现提供状态机、Topic 构造、发布队列和命令解析。远程命令默认
 * 拒绝执行，直到签名、时间戳和序列号策略确认。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef SERVICE_MQTT_H
#define SERVICE_MQTT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_MAX_TOPIC_LEN      128
#define MQTT_MAX_PAYLOAD_LEN    2048
#define MQTT_MAX_TOPIC_HANDLERS 8
#define MQTT_PUBLISH_QUEUE_LEN  8
#define MQTT_RECONNECT_MIN_MS   1000
#define MQTT_RECONNECT_MAX_MS   32000
#define MQTT_KEEP_ALIVE_DEFAULT_S 60

typedef enum {
    SERVICE_MQTT_OK                =  0,
    SERVICE_MQTT_ERR_PARAM         = -1,
    SERVICE_MQTT_ERR_NOT_INIT      = -2,
    SERVICE_MQTT_ERR_ALREADY_INIT  = -3,
    SERVICE_MQTT_ERR_NO_MEM        = -4,
    SERVICE_MQTT_ERR_NOT_CONNECTED = -5,
    SERVICE_MQTT_ERR_OFFLINE       = -6,
    SERVICE_MQTT_ERR_AUTH_REQUIRED = -7,
} service_mqtt_err_t;

typedef enum {
    SERVICE_MQTT_STATE_DISCONNECTED = 0,
    SERVICE_MQTT_STATE_CONNECTING,
    SERVICE_MQTT_STATE_CONNECTED,
    SERVICE_MQTT_STATE_ERROR,
} service_mqtt_state_t;

typedef enum {
    MQTT_TOPIC_EVENT = 0,
    MQTT_TOPIC_HEARTBEAT,
    MQTT_TOPIC_CMD,
    MQTT_TOPIC_LWT,
    MQTT_TOPIC_STATUS,
    MQTT_TOPIC_ACCESS,
    MQTT_TOPIC_ALARM,
    MQTT_TOPIC_USER_SET,
    MQTT_TOPIC_REPLY,
} mqtt_topic_type_t;

typedef enum {
    SERVICE_MQTT_CMD_NONE = 0,
    SERVICE_MQTT_CMD_USER_ADD,
    SERVICE_MQTT_CMD_USER_DEL,
    SERVICE_MQTT_CMD_USER_UPDATE,
    SERVICE_MQTT_CMD_CONFIG_SET,
    SERVICE_MQTT_CMD_OTA_START,
    SERVICE_MQTT_CMD_USER_SET,
    SERVICE_MQTT_CMD_REBOOT,
} service_mqtt_cmd_t;

typedef enum {
    SERVICE_MQTT_EVENT_PASS = 0,
    SERVICE_MQTT_EVENT_DENY = 1,
    SERVICE_MQTT_EVENT_DURESS = 2,
    SERVICE_MQTT_EVENT_ALARM = 3,
} service_mqtt_event_type_t;

typedef struct {
    const char *device_id;       /**< 设备 ID */
    const char *broker_uri;      /**< Broker URI */
    const char *client_id;       /**< Client ID，可为空 */
    const char *username;        /**< 用户名，可为空 */
    const char *password;        /**< 密码，可为空 */
    uint32_t reconnect_min_s;    /**< 最小重连间隔 */
    uint32_t reconnect_max_s;    /**< 最大重连间隔 */
    uint32_t keep_alive_s;       /**< keepalive 秒 */
} service_mqtt_config_t;

typedef struct {
    char     topic[MQTT_MAX_TOPIC_LEN];     /**< Topic */
    uint8_t  payload[MQTT_MAX_PAYLOAD_LEN]; /**< Payload */
    uint32_t payload_len;                   /**< Payload 长度 */
    int      qos;                           /**< QoS */
    bool     retain;                        /**< retain */
} mqtt_publish_req_t;

typedef struct {
    char     topic[MQTT_MAX_TOPIC_LEN];     /**< Topic */
    uint8_t  payload[MQTT_MAX_PAYLOAD_LEN]; /**< Payload */
    uint32_t payload_len;                   /**< Payload 长度 */
} service_mqtt_message_t;

typedef void (*service_mqtt_state_cb_t)(service_mqtt_state_t state, void *arg);
typedef void (*service_mqtt_topic_handler_t)(service_mqtt_cmd_t cmd,
                                             const service_mqtt_message_t *msg,
                                             void *arg);

typedef struct {
    service_mqtt_state_t state;      /**< 当前状态 */
    bool                 inited;     /**< 是否初始化 */
    bool                 connected;  /**< 是否连接 */
    uint32_t             pub_ok;     /**< 发布成功计数 */
    uint32_t             pub_fail;   /**< 发布失败计数 */
    uint32_t             msg_recv;   /**< 接收消息计数 */
    uint32_t             conn_count; /**< 连接次数 */
    uint32_t             disconn_count; /**< 断开次数 */
} service_mqtt_status_t;

typedef struct {
    uint32_t uptime;     /**< 运行时间 */
    uint32_t free_heap;  /**< 空闲堆 */
} service_mqtt_status_payload_t;

int service_mqtt_init(const service_mqtt_config_t *cfg);
int service_mqtt_deinit(void);
int service_mqtt_start(void);
int service_mqtt_stop(void);
int service_mqtt_publish_event(service_mqtt_event_type_t event, uint32_t uid,
                               uint8_t score, uint32_t image_id);
int service_mqtt_publish_status(const service_mqtt_status_payload_t *status);
int service_mqtt_publish_heartbeat(void);
int service_mqtt_get_status(service_mqtt_status_t *status);
service_mqtt_state_t service_mqtt_get_state(void);
bool service_mqtt_is_connected(void);
int service_mqtt_set_state_callback(service_mqtt_state_cb_t cb, void *arg);
int service_mqtt_register_topic_handler(const char *topic_filter,
                                        service_mqtt_topic_handler_t handler,
                                        void *arg);
int service_mqtt_publish(const char *topic, const uint8_t *payload,
                         uint32_t payload_len, int qos, bool retain);
int service_mqtt_subscribe(const char *topic, int qos);
int service_mqtt_handle_remote_command(const service_mqtt_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_MQTT_H */
