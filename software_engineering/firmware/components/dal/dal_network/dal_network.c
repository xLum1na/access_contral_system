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
    esp_eth_handle_t   eth_handle;
    esp_netif_t       *netif;
    bool               inited;
    bool               connected;
} dal_network_internal_t;

/* ---- 事件回调 ---- */
static void eth_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    dal_network_internal_t *c = (dal_network_internal_t *)arg;
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Link Up");
        break;
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
    dal_network_internal_t *c = (dal_network_internal_t *)arg;
    if (id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        c->connected = true;
    }
}

/* ================================================================
 *  API
 * ================================================================ */

int dal_network_init(dal_network_handle_t *handle,
                     const dal_network_config_t *cfg)
{
    if (!handle || !cfg) return -1;

    dal_network_internal_t *c = calloc(1, sizeof(*c));
    if (!c) return -1;

    esp_err_t ret;

    /* ---- 1. 初始化 TCP/IP 栈和事件循环 ---- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* ---- 2. 创建 Ethernet netif ---- */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    c->netif = esp_netif_new(&netif_cfg);
    if (!c->netif) {
        ESP_LOGE(TAG, "创建 netif 失败");
        free(c); return -1;
    }

    /* ---- 3. 创建 MAC ---- */
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num  = cfg->mdc_pin;
    emac_config.smi_gpio.mdio_num = cfg->mdio_pin;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (!mac) {
        ESP_LOGE(TAG, "创建 MAC 失败");
        esp_netif_destroy(c->netif);
        free(c); return -1;
    }

    /* ---- 4. 创建 PHY ---- */
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr       = cfg->phy_addr;
    phy_config.reset_gpio_num = cfg->phy_reset_pin;

    esp_eth_phy_t *phy = NULL;
    switch (cfg->phy_type) {
    case DAL_ETH_PHY_RTL8201:
        phy = esp_eth_phy_new_rtl8201(&phy_config); break;
    case DAL_ETH_PHY_LAN87XX:
        phy = esp_eth_phy_new_lan87xx(&phy_config); break;
    case DAL_ETH_PHY_IP101:
        phy = esp_eth_phy_new_ip101(&phy_config); break;
    case DAL_ETH_PHY_DP83848:
        phy = esp_eth_phy_new_dp83848(&phy_config); break;
    default:
        phy = esp_eth_phy_new_generic(&phy_config); break;
    }
    if (!phy) {
        ESP_LOGE(TAG, "创建 PHY 失败");
        mac->del(mac);
        esp_netif_destroy(c->netif);
        free(c); return -1;
    }

    /* ---- 5. 安装以太网驱动 ---- */
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ret = esp_eth_driver_install(&eth_config, &c->eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "驱动安装失败: %d", ret);
        phy->del(phy); mac->del(mac);
        esp_netif_destroy(c->netif);
        free(c); return ret;
    }

    /* ---- 6. 绑定 netif ---- */
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(c->eth_handle);
    if (!glue) {
        ESP_LOGE(TAG, "netif glue 失败");
        esp_eth_driver_uninstall(c->eth_handle);
        phy->del(phy); mac->del(mac);
        esp_netif_destroy(c->netif);
        free(c); return -1;
    }
    ret = esp_netif_attach(c->netif, glue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "netif attach 失败: %d", ret);
        esp_eth_del_netif_glue(glue);
        esp_eth_driver_uninstall(c->eth_handle);
        phy->del(phy); mac->del(mac);
        esp_netif_destroy(c->netif);
        free(c); return ret;
    }

    /* ---- 7. 注册事件回调 ---- */
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                                eth_event_handler, c));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                                ip_event_handler, c));

    /* ---- 8. 配置 IP ---- */
    if (!cfg->ip_cfg.use_dhcp) {
        esp_netif_ip_info_t ip_info = {0};
        esp_netif_str_to_ip4(cfg->ip_cfg.static_ip,  &ip_info.ip);
        esp_netif_str_to_ip4(cfg->ip_cfg.netmask,     &ip_info.netmask);
        esp_netif_str_to_ip4(cfg->ip_cfg.gateway,     &ip_info.gw);
        esp_netif_dhcpc_stop(c->netif);
        esp_netif_set_ip_info(c->netif, &ip_info);
    }

    /* ---- 9. 启动 ---- */
    ret = esp_eth_start(c->eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动失败: %d", ret);
        esp_eth_driver_uninstall(c->eth_handle);
        phy->del(phy); mac->del(mac);
        esp_netif_destroy(c->netif);
        free(c); return ret;
    }

    c->inited = true;
    *handle = (dal_network_handle_t)c;
    ESP_LOGI(TAG, "初始化完成");
    return 0;
}

int dal_network_deinit(dal_network_handle_t handle)
{
    dal_network_internal_t *c = (dal_network_internal_t *)handle;
    if (!c || !c->inited) return -1;
    esp_eth_stop(c->eth_handle);
    esp_eth_driver_uninstall(c->eth_handle);
    esp_netif_destroy(c->netif);
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
