/**
 * @file    dal_network.c
 * @brief   DAL 网络模块 - 以太网实现
 *
 * ESP32-P4 EMAC + RMII PHY → esp_eth → esp_netif → lwIP
 */

#include "dal_network.h"

#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

#define TAG "DAL_NET"

/* ---- 内部结构体 ---- */
typedef struct {
    esp_eth_handle_t             eth_handle;
    esp_eth_mac_t               *mac;
    esp_eth_phy_t               *phy;
    esp_eth_netif_glue_handle_t  glue;
    esp_netif_t                 *netif;
    bool                         inited;
    bool                         started;
    bool                         connected;
    bool                         eth_event_registered;
    bool                         ip_event_registered;
} dal_network_internal_t;

/* ---- 事件回调 ---- */
static void eth_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    (void)base;

    dal_network_internal_t *c = (dal_network_internal_t *)arg;
    if (!c) return;

    switch (id) {
    case ETHERNET_EVENT_CONNECTED: {
        uint8_t mac_addr[6] = {0};
        esp_eth_handle_t eth_handle = data ? *(esp_eth_handle_t *)data : c->eth_handle;
        if (eth_handle) {
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        }
        ESP_LOGI(TAG, "Link Up, MAC=%02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Link Down");
        c->connected = false;
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Stopped");
        break;
    default: break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    (void)base;

    dal_network_internal_t *c = (dal_network_internal_t *)arg;
    if (!c) return;

    if (id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR ", MASK: " IPSTR ", GW: " IPSTR,
                 IP2STR(&ev->ip_info.ip),
                 IP2STR(&ev->ip_info.netmask),
                 IP2STR(&ev->ip_info.gw));
        c->connected = true;
    }
}

/**
 * @brief 释放网络初始化过程中已申请的资源
 *
 * @param c 网络内部上下文
 */
static void dal_network_cleanup(dal_network_internal_t *c)
{
    if (!c) return;

    if (c->ip_event_registered) {
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, ip_event_handler);
        c->ip_event_registered = false;
    }
    if (c->eth_event_registered) {
        esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler);
        c->eth_event_registered = false;
    }
    if (c->started && c->eth_handle) {
        esp_eth_stop(c->eth_handle);
        c->started = false;
    }
    if (c->glue) {
        esp_eth_del_netif_glue(c->glue);
        c->glue = NULL;
    }
    if (c->eth_handle) {
        esp_eth_driver_uninstall(c->eth_handle);
        c->eth_handle = NULL;
    }
    if (c->phy) {
        c->phy->del(c->phy);
        c->phy = NULL;
    }
    if (c->mac) {
        c->mac->del(c->mac);
        c->mac = NULL;
    }
    if (c->netif) {
        esp_netif_destroy(c->netif);
        c->netif = NULL;
    }
}

/* ================================================================
 *  API
 * ================================================================ */

int dal_network_init(dal_network_handle_t *handle,
                     const dal_network_config_t *cfg)
{
    if (!handle || !cfg) return -1;
    if (cfg->mdc_pin < 0 || cfg->mdio_pin < 0) {
        ESP_LOGE(TAG, "无效的 RMII SMI 引脚: MDC=%d MDIO=%d", cfg->mdc_pin, cfg->mdio_pin);
        return -1;
    }

    dal_network_internal_t *c = calloc(1, sizeof(*c));
    if (!c) return -1;

    esp_err_t ret;

    /* ---- 1. 初始化 TCP/IP 栈和事件循环 ---- */
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init 失败: %d", ret);
        free(c);
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "默认事件循环创建失败: %d", ret);
        free(c);
        return ret;
    }

    /* ---- 2. 创建 Ethernet netif ---- */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    c->netif = esp_netif_new(&netif_cfg);
    if (!c->netif) {
        ESP_LOGE(TAG, "创建 netif 失败");
        free(c);
        return -1;
    }

    /* ---- 3. 创建 MAC ---- */
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num  = cfg->mdc_pin;
    emac_config.smi_gpio.mdio_num = cfg->mdio_pin;

    ESP_LOGI(TAG,
             "RMII cfg: MDC=%d MDIO=%d PHY_RST=%d PHY_ADDR=%d PHY_TYPE=%d",
             cfg->mdc_pin, cfg->mdio_pin,
             cfg->phy_reset_pin, cfg->phy_addr,
             (int)cfg->phy_type);

    c->mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (!c->mac) {
        ESP_LOGE(TAG, "创建 MAC 失败");
        dal_network_cleanup(c);
        free(c);
        return -1;
    }

    /* ---- 4. 创建 PHY ---- */
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr       = cfg->phy_addr;
    phy_config.reset_gpio_num = cfg->phy_reset_pin;

    switch (cfg->phy_type) {
    case DAL_ETH_PHY_RTL8201:
        c->phy = esp_eth_phy_new_rtl8201(&phy_config); break;
    case DAL_ETH_PHY_LAN87XX:
        c->phy = esp_eth_phy_new_lan87xx(&phy_config); break;
    case DAL_ETH_PHY_IP101:
        c->phy = esp_eth_phy_new_ip101(&phy_config); break;
    case DAL_ETH_PHY_DP83848:
        c->phy = esp_eth_phy_new_dp83848(&phy_config); break;
    default:
        c->phy = esp_eth_phy_new_generic(&phy_config); break;
    }
    if (!c->phy) {
        ESP_LOGE(TAG, "创建 PHY 失败");
        dal_network_cleanup(c);
        free(c);
        return -1;
    }

    /* ---- 5. 安装以太网驱动 ---- */
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(c->mac, c->phy);
    ret = esp_eth_driver_install(&eth_config, &c->eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "驱动安装失败: %d", ret);
        dal_network_cleanup(c);
        free(c);
        return ret;
    }

    /* ---- 6. 绑定 netif ---- */
    c->glue = esp_eth_new_netif_glue(c->eth_handle);
    if (!c->glue) {
        ESP_LOGE(TAG, "netif glue 失败");
        dal_network_cleanup(c);
        free(c);
        return -1;
    }

    ret = esp_netif_attach(c->netif, c->glue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "netif attach 失败: %d", ret);
        dal_network_cleanup(c);
        free(c);
        return ret;
    }

    /* ---- 7. 注册事件回调 ---- */
    ret = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                     eth_event_handler, c);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ETH 事件注册失败: %d", ret);
        dal_network_cleanup(c);
        free(c);
        return ret;
    }
    c->eth_event_registered = true;

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                     ip_event_handler, c);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IP 事件注册失败: %d", ret);
        dal_network_cleanup(c);
        free(c);
        return ret;
    }
    c->ip_event_registered = true;

    /* ---- 8. 配置 IP ---- */
    if (!cfg->ip_cfg.use_dhcp) {
        esp_netif_ip_info_t ip_info = {0};
        esp_netif_str_to_ip4(cfg->ip_cfg.static_ip, &ip_info.ip);
        esp_netif_str_to_ip4(cfg->ip_cfg.netmask, &ip_info.netmask);
        esp_netif_str_to_ip4(cfg->ip_cfg.gateway, &ip_info.gw);
        ret = esp_netif_dhcpc_stop(c->netif);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "DHCP Client 停止失败: %d", ret);
            dal_network_cleanup(c);
            free(c);
            return ret;
        }
        ret = esp_netif_set_ip_info(c->netif, &ip_info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "静态 IP 配置失败: %d", ret);
            dal_network_cleanup(c);
            free(c);
            return ret;
        }
    }

    /* ---- 9. 启动 ---- */
    ret = esp_eth_start(c->eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动失败: %d", ret);
        dal_network_cleanup(c);
        free(c);
        return ret;
    }
    c->started = true;

    c->inited = true;
    *handle = (dal_network_handle_t)c;
    ESP_LOGI(TAG, "初始化完成，等待网线 Link Up 和 DHCP 获取 IP");
    return 0;
}

int dal_network_deinit(dal_network_handle_t handle)
{
    dal_network_internal_t *c = (dal_network_internal_t *)handle;
    if (!c || !c->inited) return -1;

    c->inited = false;
    c->connected = false;
    dal_network_cleanup(c);
    free(c);
    ESP_LOGI(TAG, "已释放");
    return 0;
}

int dal_network_get_ip(dal_network_handle_t handle, char *ip, size_t ip_len)
{
    dal_network_internal_t *c = (dal_network_internal_t *)handle;
    if (!c || !c->inited || !c->netif || !ip) return -1;
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(c->netif, &ip_info) != ESP_OK) return -1;
    snprintf(ip, ip_len, IPSTR, IP2STR(&ip_info.ip));
    return 0;
}

bool dal_network_is_connected(dal_network_handle_t handle)
{
    dal_network_internal_t *c = (dal_network_internal_t *)handle;
    return c && c->inited && c->connected;
}
