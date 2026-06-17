/**
 * @file    dal_storage.c
 * @brief   DAL 存储模块 - SDMMC + FAT 实现
 */

#include "dal_storage.h"

#include "driver/sdmmc_host.h"
#include "driver/sdmmc_types.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TAG "DAL_SD"

/* ---- 内部结构体 ---- */
typedef struct {
    sdmmc_card_t *card;
    char          mount_point[16];
    bool          mounted;
} dal_storage_internal_t;

/* ================================================================
 *  API
 * ================================================================ */

int dal_storage_init(dal_storage_handle_t *handle,
                     const dal_storage_config_t *cfg)
{
    if (!handle || !cfg) return -1;

    dal_storage_internal_t *c = calloc(1, sizeof(*c));
    if (!c) return -1;
    strncpy(c->mount_point, cfg->mount_point[0] ? cfg->mount_point : "/sdcard",
            sizeof(c->mount_point) - 1);

    esp_err_t ret;

    /* SDMMC 主机配置 */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = cfg->freq_khz ? cfg->freq_khz : SDMMC_FREQ_DEFAULT;

    /* SD 卡 LDO 供电 (通道 4, 3.3V) */
    sd_pwr_ctrl_ldo_config_t ldo_cfg = { 
        .ldo_chan_id = 4,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl = NULL;
    esp_err_t ldo_ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr_ctrl);
    if (ldo_ret == ESP_OK) {
        host.pwr_ctrl_handle = pwr_ctrl;
        ESP_LOGI(TAG, "SD LDO 供电已启用");
    }

    /* SDMMC 插槽配置 */
    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    /* 若引脚全为 -1，使用 Slot 0 IO MUX (固定引脚) */
    if (cfg->clk_pin < 0) {
        /* 使用 IO MUX，不设 GPIO，SDMMC_SLOT_CONFIG_DEFAULT 已配置 */
        slot_cfg.width = 4;
        slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    } else {
        slot_cfg.clk = cfg->clk_pin;
        slot_cfg.cmd = cfg->cmd_pin;
        slot_cfg.d0  = cfg->d0_pin;
        if (!cfg->use_1bit) {
            slot_cfg.d1 = cfg->d1_pin;
            slot_cfg.d2 = cfg->d2_pin;
            slot_cfg.d3 = cfg->d3_pin;
            slot_cfg.width = 4;
        } else { slot_cfg.width = 1; }
        slot_cfg.cd = cfg->cd_pin;
        slot_cfg.wp = cfg->wp_pin;
        slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    }

    /* FAT 挂载配置 */
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = cfg->format_if_mount_failed,
        .max_files              = cfg->max_files ? cfg->max_files : 5,
        .allocation_unit_size   = 16 * 1024,
    };

    /* 挂载 */
    ret = esp_vfs_fat_sdmmc_mount(c->mount_point, &host, &slot_cfg,
                                  &mount_cfg, &c->card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "挂载失败: %s (0x%X)", esp_err_to_name(ret), ret);
        free(c);
        return ret;
    }

    c->mounted = true;
    *handle = (dal_storage_handle_t)c;

    sdmmc_card_print_info(stdout, c->card);
    ESP_LOGI(TAG, "挂载成功: %s", c->mount_point);
    return 0;
}

int dal_storage_deinit(dal_storage_handle_t handle)
{
    dal_storage_internal_t *c = (dal_storage_internal_t *)handle;
    if (!c || !c->mounted) return -1;

    esp_vfs_fat_sdcard_unmount(c->mount_point, c->card);
    free(c);
    ESP_LOGI(TAG, "已卸载");
    return 0;
}

int dal_storage_get_info(dal_storage_handle_t handle,
                         uint32_t *total_mb, uint32_t *free_mb)
{
    dal_storage_internal_t *c = (dal_storage_internal_t *)handle;
    if (!c || !c->mounted || !c->card) return -1;

    if (total_mb) {
        *total_mb = (uint32_t)(((uint64_t)c->card->csd.capacity
                     * c->card->csd.sector_size) / (1024 * 1024));
    }

    if (free_mb) {
        FATFS *fs;
        DWORD free_clust;
        if (f_getfree("0:", &free_clust, &fs) == FR_OK) {
            *free_mb = (uint32_t)((uint64_t)free_clust * fs->csize
                       * FF_SS_SDCARD / (1024 * 1024));
        } else {
            *free_mb = 0;
        }
    }
    return 0;
}

const char *dal_storage_get_mount_point(dal_storage_handle_t handle)
{
    dal_storage_internal_t *c = (dal_storage_internal_t *)handle;
    return (c && c->mounted) ? c->mount_point : NULL;
}
