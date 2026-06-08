/**
 * @file    main.c
 * @brief   DAL 全模块验证：Display + Touch + Network + Storage
 */

#include <stdio.h>
#include <string.h>

#include "dal_display.h"
#include "dal_touch.h"
#include "dal_network.h"
#include "dal_storage.h"
#include "dal_audio.h"
#include "pal_i2c.h"
#include "pal_log.h"
#include "osal_task.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
/* stb_image 使用 PSRAM 分配内存 */
#define STBI_MALLOC(sz)    heap_caps_malloc(sz, MALLOC_CAP_SPIRAM)
#define STBI_REALLOC(p,sz) heap_caps_realloc(p, sz, MALLOC_CAP_SPIRAM)
#define STBI_FREE(p)       heap_caps_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <math.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include "pal_gpio.h"

#define TAG "MAIN"

/* ================================================================
 *  硬件参数
 * ================================================================ */

#define I2C_SDA         7
#define I2C_SCL         8
#define I2C_FREQ_HZ     400000U

#define LCD_H_RES       800U
#define LCD_V_RES       480U
#define T_HSYNC_PW      2
#define T_HSYNC_BP      46
#define T_HSYNC_FP      210
#define T_VSYNC_PW      20
#define T_VSYNC_BP      4
#define T_VSYNC_FP      22
#define DSI_HOST        0
#define DSI_LANES       1
#define DSI_BITRATE     600
#define DSI_VC          0
#define DPI_CLK_MHZ     (25.98f)
#define T_PWR_BL        100
#define NUM_FBS         1
#define INIT_BL_PCT     80
#define DUT_ATTINY88    0x45
#define DUT_FT5406      0x38

/* ---- 网络 (IP101 PHY) ---- */
#define ETH_MDC         31
#define ETH_MDIO        52
#define ETH_PHY_RST     51
#define ETH_PHY_ADDR    1

/* ---- SDMMC (Slot 0 IO MUX, -1=使用固定引脚) ---- */
#define SD_CLK          -1
#define SD_CMD          -1
#define SD_D0           -1
#define SD_D1           -1
#define SD_D2           -1
#define SD_D3           -1

/* ---- 音频 (ES8311 + I2S) ---- */
#define AUDIO_MCLK      13
#define AUDIO_SCLK      12
#define AUDIO_LCLK      10
#define AUDIO_DOUT      9
#define AUDIO_DIN       48
#define AUDIO_RATE      22050

static dal_display_handle_t g_display;
static dal_touch_handle_t   g_touch;
static dal_network_handle_t g_network;
static dal_storage_handle_t g_storage;
static dal_audio_handle_t  g_audio;
static pal_i2c_bus_handle_t g_i2c;
static uint8_t              *g_fb;
static size_t                g_fb_sz;

/* ================================================================
 *  帧缓冲操作
 * ================================================================ */

static void fb_fill(uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++)
        { g_fb[i*3+0]=b; g_fb[i*3+1]=g; g_fb[i*3+2]=r; }
}

static void fb_draw_dot(int x, int y, int r, uint32_t c)
{
    uint8_t rb = c & 0xFF, gb = (c>>8)&0xFF, bb = (c>>16)&0xFF;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
        if (dx*dx+dy*dy > r*r) continue;
        int px=x+dx, py=y+dy;
        if (px<0||px>=LCD_H_RES||py<0||py>=LCD_V_RES) continue;
        int i = py*LCD_H_RES+px;
        g_fb[i*3+0]=rb; g_fb[i*3+1]=gb; g_fb[i*3+2]=bb;
    }
}

static void fb_flush(void) {
    esp_cache_msync(g_fb, g_fb_sz, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

/* ================================================================
 *  app_main
 * ================================================================ */

void app_main(void)
{
    int ret;
    PAL_LOGI(TAG, "======== 全模块测试 ========");

    /* ---- I2C ---- */
    const pal_i2c_bus_config_t i2c_cfg = {
        .port = -1, .sda_pin = I2C_SDA, .scl_pin = I2C_SCL,
        .freq_hz = I2C_FREQ_HZ, .enable_internal_pullup = true,
        .trans_queue_depth = 0,
    };
    ret = pal_i2c_bus_init(&g_i2c, &i2c_cfg);
    if (ret) { PAL_LOGE(TAG, "I2C fail"); goto err; }
    for (uint8_t a = 1; a < 127; a++)
        if (pal_i2c_dev_probe(g_i2c, a) == 0) PAL_LOGI(TAG, "I2C: 0x%02X", a);

    /* ---- Display ---- */
    const dal_display_config_t dcfg = {
        .i2c_bus_handle = g_i2c, .use_attiny88 = true, .attiny88_i2c_addr = DUT_ATTINY88,
        .default_brightness = INIT_BL_PCT, .power_to_backlight_ms = T_PWR_BL,
        .dsi_host = DSI_HOST, .dsi_num_data_lanes = DSI_LANES,
        .dsi_lane_bit_rate_mbps = DSI_BITRATE, .dsi_virtual_channel = DSI_VC,
        .h_res = LCD_H_RES, .v_res = LCD_V_RES,
        .pixel_format = PAL_DSI_COLOR_RGB888, .in_color_format = PAL_DSI_IN_COLOR_RGB888,
        .num_fbs = NUM_FBS, .dpi_clock_freq_mhz = DPI_CLK_MHZ,
        .timing = { .h_res = LCD_H_RES, .v_res = LCD_V_RES,
            .hsync_pulse_width = T_HSYNC_PW, .hsync_back_porch = T_HSYNC_BP,
            .hsync_front_porch = T_HSYNC_FP, .vsync_pulse_width = T_VSYNC_PW,
            .vsync_back_porch = T_VSYNC_BP, .vsync_front_porch = T_VSYNC_FP },
    };
    ret = dal_display_init(&g_display, &dcfg);
    if (!ret) { dal_display_on(g_display); PAL_LOGI(TAG, "Display OK"); }
    else { PAL_LOGW(TAG, "Display fail: %d", ret); g_display = NULL; }

    void *fb0 = NULL;
    if (g_display) { dal_display_get_fb(g_display, &fb0, NULL); g_fb = fb0; g_fb_sz = LCD_H_RES*LCD_V_RES*3; fb_fill(0x000000); fb_flush(); }

    /* ---- Touch ---- */
    const dal_touch_config_t tcfg = { .i2c_bus_handle = g_i2c, .ft5406_i2c_addr = DUT_FT5406,
                                      .h_res = LCD_H_RES, .v_res = LCD_V_RES };
    ret = dal_touch_init(&g_touch, &tcfg);
    if (!ret) PAL_LOGI(TAG, "Touch OK"); else { PAL_LOGW(TAG, "Touch fail: %d", ret); g_touch = NULL; }

    /* ---- Storage ---- */
    const dal_storage_config_t scfg = {
        .clk_pin = SD_CLK, .cmd_pin = SD_CMD, .d0_pin = SD_D0,
        .d1_pin = SD_D1, .d2_pin = SD_D2, .d3_pin = SD_D3,
        .cd_pin = -1, .wp_pin = -1, .max_files = 5, .use_1bit = false,
        .mount_point = "/sdcard",
    };
    ret = dal_storage_init(&g_storage, &scfg);
    if (!ret) { uint32_t t=0, f=0; dal_storage_get_info(g_storage, &t, &f); PAL_LOGI(TAG, "Storage OK: %luMB total, %luMB free", t, f); }
    else { PAL_LOGW(TAG, "Storage fail: %d", ret); g_storage = NULL; }

    /* ---- JPEG 显示：自动寻找 SD 卡第一个 jpg ---- */
    if (g_storage && g_fb) {
        char jpg_path[128] = {0};
        DIR *d = opendir("/sdcard");
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                const char *n = de->d_name;
                size_t len = strlen(n);
                bool is_img = false;
                if (len > 4 && len < 120) {
                    const char *ext = n + len - 4;
                    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".png") == 0) is_img = true;
                }
                if (is_img) {
                    strcpy(jpg_path, "/sdcard/");
                    strcat(jpg_path, n);
                    break;
                }
            }
            closedir(d);
        }
        if (jpg_path[0]) {
            PAL_LOGI(TAG, "JPG: %s", jpg_path);
            FILE *f = fopen(jpg_path, "rb");
            if (!f) { PAL_LOGE(TAG, "fopen fail"); }
            else {
                fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
                PAL_LOGI(TAG, "fsize=%ld", fsz);
                uint8_t *jbuf = heap_caps_malloc(fsz, MALLOC_CAP_SPIRAM);
                if (!jbuf) { PAL_LOGE(TAG, "malloc(%ld) fail", fsz); }
                else if (fread(jbuf, 1, fsz, f) != (size_t)fsz) { PAL_LOGE(TAG, "fread fail"); }
                else {
                    int w, h, comp;
                    /* 先获取尺寸，超过 1920x1080 不解码 */
                    if (!stbi_info_from_memory(jbuf, fsz, &w, &h, &comp)) {
                        PAL_LOGE(TAG, "info fail: %s", stbi_failure_reason());
                    } else if (w > 1920 || h > 1080 || w*h > 1920*1080) {
                        PAL_LOGW(TAG, "图片太大 %dx%d, 跳过", w, h);
                    } else {
                    uint8_t *rgb = stbi_load_from_memory(jbuf, fsz, &w, &h, &comp, 3);
                    if (!rgb) {
                        PAL_LOGE(TAG, "decode fail: %s", stbi_failure_reason());
                    } else {
                        PAL_LOGI(TAG, "IMG: %dx%d", w, h);
                        int ox = (LCD_H_RES-w)/2, oy = (LCD_V_RES-h)/2;
                        if (ox<0) { ox=0; } if (oy<0) { oy=0; }
                        int cw = w>LCD_H_RES?LCD_H_RES:w;
                        int ch = h>LCD_V_RES?LCD_V_RES:h;
                        for (int y=0; y<ch; y++)
                            memcpy(g_fb+((oy+y)*LCD_H_RES+ox)*3, rgb+y*w*3, cw*3);
                        fb_flush();
                        stbi_image_free(rgb);
                        PAL_LOGI(TAG, "显示完成");
                    }
                    } /* end stbi_load */
                    } /* end size check */
                free(jbuf); fclose(f);
            }
        } else { PAL_LOGW(TAG, "无图片文件"); }
    }

    PAL_LOGI(TAG, "======== 运行中 ========");

    const uint32_t colors[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000 };
    int ci = 0;
    while (1) {
        if (g_touch && g_fb) {
            dal_touch_data_t td;
            if (dal_touch_read(g_touch, &td) > 0) {
                fb_draw_dot(td.points[0].x, td.points[0].y, 12, colors[ci]);
                fb_flush(); ci = (ci+1)%5;
            }
        }
        osal_task_delay_ms(5);
    }

err:
    PAL_LOGE(TAG, "错误挂起"); while(1) osal_task_delay_ms(1000);
}
