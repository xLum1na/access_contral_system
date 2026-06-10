/**
 * @file    pal_log.h (mock)
 * @brief   宿主机测试用 mock pal_log — 输出到 stdout
 */
#ifndef MOCK_PAL_LOG_H
#define MOCK_PAL_LOG_H

#include <stdio.h>

#define PAL_LOGI(t,f,...)  printf(" [INFO]  " t ": " f "\n", ##__VA_ARGS__)
#define PAL_LOGW(t,f,...)  printf(" [WARN]  " t ": " f "\n", ##__VA_ARGS__)
#define PAL_LOGE(t,f,...)  printf(" [ERROR] " t ": " f "\n", ##__VA_ARGS__)
#define PAL_LOGD(t,f,...)  printf(" [DEBUG] " t ": " f "\n", ##__VA_ARGS__)

#endif /* MOCK_PAL_LOG_H */
