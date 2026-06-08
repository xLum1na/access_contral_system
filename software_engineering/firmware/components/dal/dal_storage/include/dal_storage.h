/**
 * @file    dal_storage.h
 * @brief   DAL 存储模块 — SD 卡 (SDMMC) 抽象层
 *
 * 封装 ESP-IDF sdmmc + FAT 文件系统，提供 SD 卡挂载 / 卸载。
 * 挂载后通过标准 POSIX 文件 API (fopen/fread/fwrite) 操作文件。
 *
 * 硬件：ESP32-P4 SDMMC Host + microSD (4-bit)
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef DAL_STORAGE_H
#define DAL_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *dal_storage_handle_t;

/* ================================================================
 *  配置结构体
 * ================================================================ */

/** @brief SDMMC 引脚配置 */
typedef struct {
    int clk_pin;         /**< CLK 引脚 */
    int cmd_pin;         /**< CMD 引脚 */
    int d0_pin;          /**< D0 引脚 */
    int d1_pin;          /**< D1 引脚 (4-bit 模式, -1 不使用) */
    int d2_pin;          /**< D2 引脚 (4-bit 模式, -1 不使用) */
    int d3_pin;          /**< D3 引脚 (4-bit 模式, -1 不使用) */
    int cd_pin;          /**< Card Detect 引脚 (-1 不使用) */
    int wp_pin;          /**< Write Protect 引脚 (-1 不使用) */

    /* 选项 */
    bool     format_if_mount_failed; /**< 挂载失败时格式化 */
    int      max_files;              /**< 最大同时打开文件数 */
    uint32_t freq_khz;               /**< SDMMC 时钟频率 (kHz), 0=默认 20MHz */
    bool     use_1bit;               /**< 使用 1-bit 模式 (默认 4-bit) */
    char     mount_point[16];        /**< 挂载点路径 (如 "/sdcard") */
} dal_storage_config_t;

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 初始化 SDMMC 并挂载 FAT 文件系统
 *
 * @param[out] handle 存储句柄
 * @param[in]  cfg    SDMMC 引脚和选项
 * @return 0 成功，负数失败
 */
int dal_storage_init(dal_storage_handle_t *handle,
                     const dal_storage_config_t *cfg);

/**
 * @brief 卸载文件系统 + 释放 SDMMC
 */
int dal_storage_deinit(dal_storage_handle_t handle);

/**
 * @brief 获取 SD 卡总容量和剩余空间
 *
 * @param[out] total_mb 总容量 (MB)
 * @param[out] free_mb  剩余空间 (MB)
 * @return 0 成功
 */
int dal_storage_get_info(dal_storage_handle_t handle,
                         uint32_t *total_mb, uint32_t *free_mb);

/**
 * @brief 获取挂载点路径
 */
const char *dal_storage_get_mount_point(dal_storage_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* DAL_STORAGE_H */
