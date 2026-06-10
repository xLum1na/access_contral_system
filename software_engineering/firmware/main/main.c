/**
 * @file    main.c
 * @brief   IMX219 MIPI CSI-RX 出图测试
 *
 * ★ CSI DMA 工作流：
 *   1. start() 调用 on_get_new_trans → 获取用户 buffer → DMA 写入
 *   2. DMA 完成 ISR → on_trans_finished 通知用户 → 再次 on_get_new_trans 获取下一个 buffer
 *      → 重新配置 DMA → 循环
 *   3. 用户通过 esp_cam_ctlr_receive() 预排队 buffer，供 ISR 中 dequeue 使用
 *
 *   注意：若不用 on_get_new_trans，首帧会丢到 backup buffer 且不触发回调。
 *
 * IMX219 PLL: INCK=24MHz, VCO_OP=912MHz, lane data rate=912Mbps
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_cam_sensor.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_sccb_i2c.h"
#include "imx219.h"
#include "soc/mipi_csi_bridge_reg.h"    /* CSI_BRG_xxx */
#include "soc/mipi_csi_host_reg.h"      /* CSI_HOST_INT_ST_MAIN_REG */

static const char *TAG = "imx219";

#define I2C_SDA_IO      GPIO_NUM_7
#define I2C_SCL_IO      GPIO_NUM_8
#define CAM_XCLK_IO     GPIO_NUM_47

/* ---- 帧上下文 ---- */
typedef struct {
    SemaphoreHandle_t sem;
    uint8_t  *buf[2];       /* 双缓冲 */
    size_t    buf_sz;
    int       idx;          /* 当前填充缓冲索引 */
    int       done[2];      /* done[0]/[1]: 对应缓冲是否就绪 */
    size_t    recv[2];      /* 实际接收长度 */
} cap_ctx_t;

static cap_ctx_t g_ctx;

/* ================================================================
 *  CSI 回调 (IRAM 安全 —— 不能调 ESP_LOGI)
 * ================================================================ */

static bool IRAM_ATTR on_get_new_trans(esp_cam_ctlr_handle_t h,
                                        esp_cam_ctlr_trans_t *t, void *ud)
{
    cap_ctx_t *c = (cap_ctx_t *)ud;
    t->buffer        = c->buf[c->idx];
    t->buflen        = c->buf_sz;
    t->received_size = 0;
    return false;
}

static bool IRAM_ATTR on_trans_finished(esp_cam_ctlr_handle_t h,
                                         esp_cam_ctlr_trans_t *t, void *ud)
{
    cap_ctx_t *c = (cap_ctx_t *)ud;
    BaseType_t woken = pdFALSE;

    /* 记录当前帧 */
    int cur = c->idx;
    c->recv[cur] = t->received_size;
    c->done[cur] = 1;

    /* 切换到下一个缓冲 (on_get_new_trans 下次被调时用到) */
    c->idx ^= 1;

    /* 通知主线程 */
    xSemaphoreGiveFromISR(c->sem, &woken);
    return woken == pdTRUE;
}

/* ================================================================
 *  辅助
 * ================================================================ */

static void hex_dump(const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i += 16) {
        printf("  %04zX:", i);
        for (size_t j = 0; j < 16 && i+j < n; j++) printf(" %02X", d[i+j]);
        printf("\n");
    }
}

/* ================================================================
 *  app_main
 * ================================================================ */

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "=== IMX219 MIPI CSI 出图 ===");

    /* -- I2C -- */
    i2c_master_bus_config_t ic = {.clk_source=I2C_CLK_SRC_DEFAULT, .i2c_port=I2C_NUM_0,
        .scl_io_num=I2C_SCL_IO, .sda_io_num=I2C_SDA_IO,
        .glitch_ignore_cnt=7, .flags.enable_internal_pullup=true};
    i2c_master_bus_handle_t i2c; ESP_ERROR_CHECK(i2c_new_master_bus(&ic, &i2c));
    sccb_i2c_config_t sc = {.dev_addr_length=I2C_ADDR_BIT_LEN_7, .device_address=IMX219_SCCB_ADDR,
        .scl_speed_hz=100000, .addr_bits_width=16, .val_bits_width=8};
    esp_sccb_io_handle_t sccb; ESP_ERROR_CHECK(sccb_new_i2c_io(i2c, &sc, &sccb));

    /* -- XCLK -- */
    esp_cam_sensor_xclk_handle_t xclk;
    ESP_ERROR_CHECK(esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &xclk));
    esp_cam_sensor_xclk_config_t xc = {.esp_clock_router_cfg={.xclk_pin=CAM_XCLK_IO, .xclk_freq_hz=24000000}};
    ESP_ERROR_CHECK(esp_cam_sensor_xclk_start(xclk, &xc));
    vTaskDelay(pdMS_TO_TICKS(10));

    /* -- Sensor -- */
    esp_cam_sensor_config_t scfg = {.sccb_handle=sccb, .reset_pin=-1, .pwdn_pin=-1,
        .xclk_pin=-1, .xclk_freq_hz=24000000, .sensor_port=ESP_CAM_SENSOR_MIPI_CSI};
    esp_cam_sensor_device_t *sensor = imx219_detect(&scfg);
    if (!sensor) { ESP_LOGE(TAG,"detect fail"); goto out; }
    ESP_LOGI(TAG,"sensor: %s", esp_cam_sensor_get_name(sensor));

    ESP_ERROR_CHECK(esp_cam_sensor_set_format(sensor, NULL));
    esp_cam_sensor_format_t fmt; ESP_ERROR_CHECK(esp_cam_sensor_get_format(sensor, &fmt));
    int w = fmt.width, h = fmt.height, lane = fmt.mipi_info.lane_num;
    /* IMX219 MIPI: 实际 lane data rate = 912 Mbps (link_freq 456MHz × DDR)
     * ESP32-P4 CSI PHY 的 lane_bit_rate_mbps 应是 data rate */
    int lane_mbps = 912;
    ESP_LOGI(TAG,"fmt: %dx%d lane=%d mipi=%dMbps", w, h, lane, lane_mbps);

    /* -- CSI controller -- */
    esp_cam_ctlr_csi_config_t cc = {
        .ctlr_id=0, .clk_src=MIPI_CSI_PHY_CLK_SRC_DEFAULT,
        .h_res=w, .v_res=h, .data_lane_num=lane, .lane_bit_rate_mbps=lane_mbps,
        .input_data_color_type=CAM_CTLR_COLOR_RAW10,
        .output_data_color_type=CAM_CTLR_COLOR_RAW10,
        .queue_items=3, .bk_buffer_dis=false,
    };
    esp_cam_ctlr_handle_t csi;
    ESP_ERROR_CHECK(esp_cam_new_csi_ctlr(&cc, &csi));

    /* -- 分配双缓冲 -- */
    g_ctx.buf_sz = w * h * 10 / 8 + 4096;
    g_ctx.buf[0] = heap_caps_aligned_alloc(64, g_ctx.buf_sz, MALLOC_CAP_SPIRAM|MALLOC_CAP_DMA);
    g_ctx.buf[1] = heap_caps_aligned_alloc(64, g_ctx.buf_sz, MALLOC_CAP_SPIRAM|MALLOC_CAP_DMA);
    g_ctx.sem    = xSemaphoreCreateBinary();
    g_ctx.idx    = 0;
    g_ctx.done[0] = g_ctx.done[1] = 0;
    ESP_LOGI(TAG,"buf[0]=%p buf[1]=%p sz=%zu", (void*)g_ctx.buf[0], (void*)g_ctx.buf[1], g_ctx.buf_sz);

    /* -- 注册回调（★ on_get_new_trans 是必须的） -- */
    esp_cam_ctlr_evt_cbs_t cb = {
        .on_get_new_trans  = on_get_new_trans,
        .on_trans_finished = on_trans_finished,
    };
    ESP_ERROR_CHECK(esp_cam_ctlr_register_event_callbacks(csi, &cb, &g_ctx));

    /* ★ 手动使能 CSI bridge 时钟 (ESP-IDF v5.5 CSI 驱动遗漏了这个步骤) */
    ESP_LOGI(TAG, "enable CSI bridge clock...");
    REG_SET_BIT(CSI_BRG_CLK_EN_REG, CSI_BRG_CLK_EN);
    ESP_LOGI(TAG, "CLK_EN=0x%08" PRIX32, REG_READ(CSI_BRG_CLK_EN_REG));

    /* -- 启用 + 启动 CSI (on_get_new_trans 在 start 内部被调，提供首帧 buffer) -- */
    ESP_ERROR_CHECK(esp_cam_ctlr_enable(csi));
    ESP_ERROR_CHECK(esp_cam_ctlr_start(csi));
    vTaskDelay(pdMS_TO_TICKS(10));

    /* -- 启用 test pattern 排除传感器侧问题 -- */
    int tp = 1;
    esp_cam_sensor_ioctl(sensor, ESP_CAM_SENSOR_IOC_S_TEST_PATTERN, &tp);
    /* 回读验证 test pattern 寄存器 */
    esp_cam_sensor_reg_val_t tpr = {.regaddr = 0x0600};
    esp_cam_sensor_ioctl(sensor, ESP_CAM_SENSOR_IOC_G_REG, &tpr);
    ESP_LOGI(TAG,"test_pattern reg 0x0600 = 0x%02" PRIX32 " (expect 0x01)", tpr.value);

    /* -- 传感器 streaming ON -- */
    ESP_LOGI(TAG,"stream ON...");
    int on = 1;
    ESP_ERROR_CHECK(esp_cam_sensor_ioctl(sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &on));
    vTaskDelay(pdMS_TO_TICKS(200));

    /* 验证 streaming + dump MIPI 关键寄存器 */
    {   /* 局部作用域 */
        uint16_t regs[]={0x0100,0x0114,0x0128,0x0301,0x0304,0x0305,0x0306,0x0307,0x030B,0x030C,0x030D,0x0309,0x018C,0x018D};
        const char *names[]={"MODE","LANE","DPHY","VTP","PRVT","PROP","VTMH","VTML","OPDV","OPMH","OPML","OPXC","FMTH","FMTL"};
        for (int i=0; i<14; i++) {
            esp_cam_sensor_reg_val_t r={.regaddr=regs[i]};
            esp_cam_sensor_ioctl(sensor, ESP_CAM_SENSOR_IOC_G_REG, &r);
            ESP_LOGI(TAG,"reg 0x%04X %-4s = 0x%02" PRIX32, regs[i], names[i], r.value);
        }
    }

    /* -- 读 CSI bridge 状态 (检查硬件是否使能 + 有无数据) -- */
    /* CSI bridge 全状态 */
    /* CSI bridge 状态 */
    ESP_LOGI(TAG, "CSI_BRG: CLK_EN=0x%08" PRIX32 " CSI_EN=0x%08" PRIX32,
             REG_READ(CSI_BRG_CLK_EN_REG), REG_READ(CSI_BRG_CSI_EN_REG));
    ESP_LOGI(TAG, "CSI_BRG: INT_ENA=0x%08" PRIX32 " INT_RAW=0x%08" PRIX32 " INT_ST=0x%08" PRIX32,
             REG_READ(CSI_BRG_INT_ENA_REG), REG_READ(CSI_BRG_INT_RAW_REG), REG_READ(CSI_BRG_INT_ST_REG));
    /* CSI HOST 状态 (PHY 致命错误?) */
    ESP_LOGI(TAG, "CSI_HOST: INT_ST_MAIN=0x%08" PRIX32 " N_LANES=0x%08" PRIX32,
             REG_READ(CSI_HOST_INT_ST_MAIN_REG), REG_READ(CSI_HOST_N_LANES_REG));

    /* -- 等待帧 -- */
    ESP_LOGI(TAG,"waiting for frames...");
    for (int i = 0; i < 3; i++) {
        if (xSemaphoreTake(g_ctx.sem, pdMS_TO_TICKS(4000)) == pdTRUE) {
            /* 找到已完成的缓冲 */
            for (int j = 0; j < 2; j++) {
                if (g_ctx.done[j]) {
                    ESP_LOGI(TAG,"✓ frame[%d] recv=%zu bytes", j, g_ctx.recv[j]);
                    if (g_ctx.recv[j] > 0) {
                        /* hex dump 前 128B */
                        size_t n = g_ctx.recv[j] < 128 ? g_ctx.recv[j] : 128;
                        hex_dump(g_ctx.buf[j], n);

                        /* 统计 */
                        uint32_t s=0, mn=0xFFFF, mx=0;
                        for (size_t k=0; k<g_ctx.recv[j] && k<4096; k++) {
                            s+=g_ctx.buf[j][k];
                            if (g_ctx.buf[j][k]<mn) mn=g_ctx.buf[j][k];
                            if (g_ctx.buf[j][k]>mx) mx=g_ctx.buf[j][k];
                        }
                        ESP_LOGI(TAG,"  first 4KB: avg=%" PRIu32 " min=%" PRIu32 " max=%" PRIu32,
                                 s/4096, mn, mx);
                    }
                    g_ctx.done[j] = 0;
                    break;
                }
            }
        } else {
            ESP_LOGW(TAG,"timeout frame %d", i+1);
        }
    }

    /* cleanup */
    on = 0; esp_cam_sensor_ioctl(sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &on);
    esp_cam_ctlr_stop(csi); esp_cam_ctlr_disable(csi); esp_cam_ctlr_del(csi);
    heap_caps_free(g_ctx.buf[0]); heap_caps_free(g_ctx.buf[1]);
    vSemaphoreDelete(g_ctx.sem);
out:
    esp_cam_sensor_del_dev(sensor);
    esp_cam_sensor_xclk_stop(xclk); esp_cam_sensor_xclk_free(xclk);
    esp_sccb_del_i2c_io(sccb); i2c_del_master_bus(i2c);
    ESP_LOGI(TAG,"done");
}
