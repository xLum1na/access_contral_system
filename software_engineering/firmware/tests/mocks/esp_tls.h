/**
 * @file    esp_tls.h
 * @brief   Mock — ESP TLS 头文件（宿主机测试用）
 *
 * service_mqtt.c 不直接使用 ESP-TLS API，仅通过 MQTT 事件句柄
 * 访问错误码字段（已在 mqtt_client.h 中定义）。
 */

#ifndef MOCK_ESP_TLS_H
#define MOCK_ESP_TLS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 所有需要的类型已在 mqtt_client.h 的 esp_mqtt_error_codes_t 中定义 */

#ifdef __cplusplus
}
#endif

#endif /* MOCK_ESP_TLS_H */
