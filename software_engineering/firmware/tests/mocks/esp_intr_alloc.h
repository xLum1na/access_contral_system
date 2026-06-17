/**
 * @file    esp_intr_alloc.h
 * @brief   Mock esp_intr_alloc.h — ESP-IDF 中断分配标志（宿主机测试用）
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef MOCK_ESP_INTR_ALLOC_H
#define MOCK_ESP_INTR_ALLOC_H

#define ESP_INTR_FLAG_IRAM 0x00000001

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#endif /* MOCK_ESP_INTR_ALLOC_H */
