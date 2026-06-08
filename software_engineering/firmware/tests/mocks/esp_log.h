/**
 * @file    esp_log.h
 * @brief   Mock esp_log.h — ESP-IDF 日志（宿主机测试用）
 *
 * 将 ESP_LOGx 宏重定向到 printf，便于测试时查看日志输出。
 */

#ifndef MOCK_ESP_LOG_H
#define MOCK_ESP_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief ESP 日志级别 */
typedef enum {
    ESP_LOG_NONE  = 0,
    ESP_LOG_ERROR = 1,
    ESP_LOG_WARN  = 2,
    ESP_LOG_INFO  = 3,
    ESP_LOG_DEBUG = 4,
    ESP_LOG_VERBOSE = 5,
} esp_log_level_t;

/** @brief 运行时设置日志级别（空实现） */
static inline void esp_log_level_set(const char *tag, esp_log_level_t level)
{
    (void)tag;
    (void)level;
}

/** @brief 写日志（空实现，测试中不输出） */
static inline void esp_log_write(esp_log_level_t level, const char *tag,
                                 const char *format, ...)
{
    (void)level;
    (void)tag;
    (void)format;
}

/* 日志宏 — 测试中可选输出到 stdout 以辅助调试 */
#define ESP_LOGE(tag, fmt, ...)  printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...)  printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...)  printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...)  /* 测试中默认关闭调试日志 */

#ifdef __cplusplus
}
#endif

#endif /* MOCK_ESP_LOG_H */
