/**
 * @file    dal_network.h
 * @brief   DAL 网络模块 — 以太网抽象层
 *
 * 封装 ESP-IDF esp_eth + esp_netif，提供以太网初始化 / IP 获取。
 * 支持 DHCP（默认）和静态 IP。
 *
 * 硬件：ESP32-P4 EMAC + 外部 RMII PHY
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef DAL_NETWORK_H
#define DAL_NETWORK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *dal_network_handle_t;

/* ================================================================
 *  配置结构体
 * ================================================================ */

/** @brief 以太网 PHY 芯片类型 */
typedef enum {
    DAL_ETH_PHY_GENERIC = 0,   /**< 自动检测 */
    DAL_ETH_PHY_RTL8201 = 1,   /**< Realtek RTL8201 */
    DAL_ETH_PHY_LAN87XX = 2,   /**< SMSC LAN87xx */
    DAL_ETH_PHY_IP101   = 3,   /**< IC Plus IP101 */
    DAL_ETH_PHY_DP83848 = 4,   /**< TI DP83848 */
} dal_eth_phy_type_t;

/** @brief IP 配置 */
typedef struct {
    bool     use_dhcp;          /**< DHCP 或静态 IP */
    char     static_ip[16];     /**< 静态 IP (如 "192.168.1.100") */
    char     netmask[16];       /**< 子网掩码 */
    char     gateway[16];       /**< 网关 */
} dal_network_ip_cfg_t;

/** @brief 以太网硬件配置 */
typedef struct {
    /* RMII 接口引脚 */
    int      mdc_pin;           /**< MDC 引脚 */
    int      mdio_pin;          /**< MDIO 引脚 */
    int      phy_reset_pin;     /**< PHY 复位引脚 (-1 不使用) */
    int      phy_addr;          /**< PHY 地址 (0~31, -1=自动) */
    dal_eth_phy_type_t phy_type;/**< PHY 芯片型号 */

    /* IP 配置 */
    dal_network_ip_cfg_t ip_cfg;
} dal_network_config_t;

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 初始化以太网 (MAC + PHY + IP)
 *
 * @param[out] handle 网络句柄
 * @param[in]  cfg    硬件和 IP 配置
 * @return 0 成功，负数失败
 */
int dal_network_init(dal_network_handle_t *handle,
                     const dal_network_config_t *cfg);

/**
 * @brief 反初始化（停止 + 释放资源）
 */
int dal_network_deinit(dal_network_handle_t handle);

/**
 * @brief 获取 IPv4 地址字符串
 *
 * @param handle 网络句柄
 * @param[out] ip 输出缓冲区 (至少 16 字节)
 * @return 0 成功
 */
int dal_network_get_ip(dal_network_handle_t handle, char *ip, size_t ip_len);

/**
 * @brief 检查以太网是否已连接（link up + IP 就绪）
 *
 * @return true 已连接
 */
bool dal_network_is_connected(dal_network_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* DAL_NETWORK_H */
