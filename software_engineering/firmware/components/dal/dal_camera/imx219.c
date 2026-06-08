/**
 * @file    imx219.c
 * @brief   IMX219 传感器驱动 - 寄存器表驱动实现
 *
 * 使用完整寄存器初始化表（参考 Linux 内核驱动 + Raspberry Pi 实现）。
 * IMX219 需要特定的"magic"寄存器序列才能正常输出 MIPI 数据。
 */

#include "imx219.h"

#include "pal_i2c.h"
#include "pal_log.h"
#include "osal_task.h"
#include "driver/ledc.h"

#include <stdlib.h>
#include <string.h>

#define TAG "IMX219"

/* ---- 寄存器地址（16-bit） ---- */
#define REG_MODE_SELECT      0x0100
#define REG_SW_RESET         0x0103
#define REG_CHIP_ID_H        0x0000
#define REG_CHIP_ID_L        0x0001

/* ---- 期望的芯片 ID ---- */
#define IMX219_CHIP_ID        0x0219

/* ---- 模式选择 ---- */
#define MODE_STANDBY          0x00
#define MODE_STREAMING        0x01

/* ---- 前向声明 ---- */
static int imx219_write_reg(imx219_handle_t h, uint16_t reg, uint8_t val);
static int imx219_read_reg(imx219_handle_t h, uint16_t reg, uint8_t *val);
static int imx219_set_mode_1920x1080(imx219_handle_t h);

/* ================================================================
 *  寄存器表项
 * ================================================================ */

typedef struct {
    uint16_t reg;
    uint8_t  val;
} imx219_reg_t;

/**
 * @brief COMMON 寄存器表 - 所有模式共用（进入 standby 后写入）
 *
 * 参考 Linux 内核 imx219.c: imx219_common_regs[]
 * 这些是厂商提供的"magic"寄存器值，确保传感器模拟/数字电路正常工作。
 */
static const imx219_reg_t s_common_regs[] = {
    /* 参考 Linux 内核: imx219_common_regs[] */
    {0x0100, 0x00},   /* MODE_SELECT = standby */
    /* 高端地址访问使能序列 */
    {0x30EB, 0x05},
    {0x30EB, 0x0C},
    {0x300A, 0xFF},
    {0x300B, 0xFF},
    {0x30EB, 0x05},
    {0x30EB, 0x09},
    /* 未公开寄存器 */
    {0x455E, 0x00},
    {0x471E, 0x4B},
    {0x4767, 0x0F},
    {0x4750, 0x14},
    {0x4540, 0x00},   /* Linux=0x00 (不是 0x05) */
    {0x47B4, 0x14},
    {0x4713, 0x30},
    {0x478B, 0x10},
    {0x478F, 0x10},
    {0x4793, 0x10},
    {0x4797, 0x0E},
    {0x479B, 0x0E},
    /* X/Y_ODD_INC = 1 */
    {0x0170, 0x01},   /* X_ODD_INC (Linux: 0x0170) */
    {0x0171, 0x01},   /* Y_ODD_INC (Linux: 0x0171) */
    /* DPHY_CTRL = auto timing */
    {0x0128, 0x00},   /* auto */
    /* EXCK_FREQ = 24MHz × 256 = 0x1800 */
    {0x012A, 0x18},   /* 24*256 高字节 */
    {0x012B, 0x00},   /* 24*256 低字节 */
    /* sentinel */
    {0x0000, 0x00},
};

/** 2-lane PLL + CSI 配置 (Linux: imx219_2lane_regs[]) */
static const imx219_reg_t s_2lane_regs[] = {
    {0x0301, 0x05},   /* VTPXCK_DIV = 5 */
    {0x0303, 0x01},   /* VTSYCK_DIV = 1 */
    {0x0304, 0x03},   /* PREPLLCK_VT_DIV = 3 */
    {0x0305, 0x03},   /* PREPLLCK_OP_DIV = 3 */
    {0x0306, 0x00},   /* PLL_VT_MPY 高 */
    {0x0307, 0x39},   /* PLL_VT_MPY 低 = 57 */
    {0x030B, 0x01},   /* OPSYCK_DIV = 1 */
    {0x030C, 0x00},   /* PLL_OP_MPY 高 */
    {0x030D, 0x72},   /* PLL_OP_MPY 低 = 114 */
    {0x0114, 0x01},   /* CSI_LANE_MODE = 2-lane */
    {0x0000, 0x00},
};

/**
 * @brief MODE 寄存器表 - 1920×1080 RAW10, 2×2 binning, 30fps, 2-lane
 *
 * 参考 1080p mode:
 *   - 2×2 binning from full 3280×2464 → 1640×1232
 *   - then crop to 1920×1080 (or scale)
 */
/**
 * 简易 MODE: 小分辨率 RAW8 诊断用
 */
static int imx219_set_mode_simple(imx219_handle_t handle,
                                  uint16_t w, uint16_t h, uint8_t bpp)
{
    int ret;
    /* Binning: 2×2 analog */
    ret  = imx219_write_reg(handle, 0x0174, 0x03);
    ret |= imx219_write_reg(handle, 0x0175, 0x03);
    /* Output size */
    ret |= imx219_write_reg(handle, 0x016C, (w >> 8) & 0xFF);
    ret |= imx219_write_reg(handle, 0x016D, w & 0xFF);
    ret |= imx219_write_reg(handle, 0x016E, (h >> 8) & 0xFF);
    ret |= imx219_write_reg(handle, 0x016F, h & 0xFF);
    /* Timing */
    ret |= imx219_write_reg(handle, 0x0160, 0x06);
    ret |= imx219_write_reg(handle, 0x0161, 0xAB);
    ret |= imx219_write_reg(handle, 0x0162, 0x0D);
    ret |= imx219_write_reg(handle, 0x0163, 0x78);
    /* Data format: bpp << 8 | bpp */
    ret |= imx219_write_reg(handle, 0x018C, bpp);
    ret |= imx219_write_reg(handle, 0x018D, bpp);
    ret |= imx219_write_reg(handle, 0x0309, bpp);
    /* Test pattern off for diagnostic */
    ret |= imx219_write_reg(handle, 0x0600, 0x00);
    ret |= imx219_write_reg(handle, 0x0601, 0x00);
    return ret;
}

/**
 * MODE 表: 1920×1080, 2×2 analog binning, RAW10, 30fps, 2-lane
 *
 * 寄存器地址对应 Linux 内核:
 *   X_ADD_STA=0x0164, X_ADD_END=0x0166, Y_ADD_STA=0x0168, Y_ADD_END=0x016A
 *   X_OUTPUT_SIZE=0x016C, Y_OUTPUT_SIZE=0x016E
 *   X_ODD_INC=0x0170, Y_ODD_INC=0x0171
 *   BINNING_MODE_H=0x0174, BINNING_MODE_V=0x0175
 *   CSI_DATA_FORMAT_A=0x018C  ← 关键！
 *   OPPXCK_DIV=0x0309 ← 按 bpp 设置
 *   FRM_LENGTH_A=0x0160, LINE_LENGTH_A=0x0162
 *   ORIENTATION=0x0172
 */
static int imx219_set_mode_1920x1080(imx219_handle_t h)
{
    int ret;
    /* Crop region: full sensor (0,0) → (3280,2464) */
    ret  = imx219_write_reg(h, 0x0164, 0x00);  /* X_ADD_STA_H */
    ret |= imx219_write_reg(h, 0x0165, 0x00);  /* X_ADD_STA_L */
    ret |= imx219_write_reg(h, 0x0166, 0x0C);  /* X_ADD_END_H = 0x0CD0=3280 */
    ret |= imx219_write_reg(h, 0x0167, 0xD0);  /* X_ADD_END_L */
    ret |= imx219_write_reg(h, 0x0168, 0x00);  /* Y_ADD_STA_H */
    ret |= imx219_write_reg(h, 0x0169, 0x00);  /* Y_ADD_STA_L */
    ret |= imx219_write_reg(h, 0x016A, 0x09);  /* Y_ADD_END_H = 0x09A0=2464 */
    ret |= imx219_write_reg(h, 0x016B, 0xA0);  /* Y_ADD_END_L */
    if (ret) return ret;

    /* Output size: 1920×1080 */
    ret  = imx219_write_reg(h, 0x016C, 0x07);  /* X_OUTPUT_SIZE_H = 1920 */
    ret |= imx219_write_reg(h, 0x016D, 0x80);  /* X_OUTPUT_SIZE_L */
    ret |= imx219_write_reg(h, 0x016E, 0x04);  /* Y_OUTPUT_SIZE_H = 1080 */
    ret |= imx219_write_reg(h, 0x016F, 0x38);  /* Y_OUTPUT_SIZE_L */
    if (ret) return ret;

    /* 2×2 analog binning */
    ret  = imx219_write_reg(h, 0x0174, 0x03);  /* BINNING_MODE_H = X2_ANALOG */
    ret |= imx219_write_reg(h, 0x0175, 0x03);  /* BINNING_MODE_V = X2_ANALOG */
    if (ret) return ret;

    /* Frame timing: FLL=1763, LLP=3448 (for 30fps) */
    ret  = imx219_write_reg(h, 0x0160, 0x06);  /* FRM_LENGTH_H = 0x06E3=1763 */
    ret |= imx219_write_reg(h, 0x0161, 0xE3);
    ret |= imx219_write_reg(h, 0x0162, 0x0D);  /* LINE_LENGTH_H = 0x0D78=3448 */
    ret |= imx219_write_reg(h, 0x0163, 0x78);
    if (ret) return ret;

    /* CSI data format: RAW10 → (bpp<<8) | bpp = 0x0A0A */
    ret  = imx219_write_reg(h, 0x018C, 0x0A);  /* CSI_DATA_FORMAT_A_H */
    ret |= imx219_write_reg(h, 0x018D, 0x0A);  /* CSI_DATA_FORMAT_A_L */
    /* OPPXCK_DIV = bpp = 10 (for RAW10) */
    ret |= imx219_write_reg(h, 0x0309, 0x0A);  /* OPPXCK_DIV */
    if (ret) return ret;

    /* Test pattern window = output size */
    ret  = imx219_write_reg(h, 0x0624, 0x07);  /* TP_WINDOW_WIDTH_H */
    ret |= imx219_write_reg(h, 0x0625, 0x80);
    ret |= imx219_write_reg(h, 0x0626, 0x04);  /* TP_WINDOW_HEIGHT_H */
    ret |= imx219_write_reg(h, 0x0627, 0x38);
    /* Test pattern: 2=color bars (诊断 MIPI 发射器) */
    ret |= imx219_write_reg(h, 0x0600, 0x00);
    ret |= imx219_write_reg(h, 0x0601, 0x02);
    /* Orientation */
    ret |= imx219_write_reg(h, 0x0172, 0x00);
    return ret;
}

/* ================================================================
 *  内部结构体
 * ================================================================ */

typedef struct imx219_dev {
    pal_i2c_dev_handle_t i2c_dev;
    imx219_config_t      cfg;
    bool                 inited;
} imx219_dev_t;

/* ================================================================
 *  内部：16-bit 寄存器读写
 * ================================================================ */

static int imx219_write_reg(imx219_handle_t h, uint16_t reg, uint8_t val)
{
    imx219_dev_t *d = (imx219_dev_t *)h;
    if (!d || !d->i2c_dev) return -1;
    uint8_t buf[3] = { (reg >> 8) & 0xFF, reg & 0xFF, val };
    return pal_i2c_write(d->i2c_dev, buf, sizeof(buf));
}

static int imx219_read_reg(imx219_handle_t h, uint16_t reg, uint8_t *val)
{
    imx219_dev_t *d = (imx219_dev_t *)h;
    if (!d || !d->i2c_dev || !val) return -1;
    uint8_t addr_buf[2] = { (reg >> 8) & 0xFF, reg & 0xFF };
    int ret = pal_i2c_write(d->i2c_dev, addr_buf, sizeof(addr_buf));
    if (ret) return ret;
    return pal_i2c_read(d->i2c_dev, val, 1);
}

/**
 * @brief 批量写寄存器表（直到 sentinel {0x0000, 0x00}）
 */
static int imx219_write_table(imx219_handle_t h, const imx219_reg_t *table)
{
    int ret;
    for (; table->reg != 0x0000 || table->val != 0x00; table++) {
        ret = imx219_write_reg(h, table->reg, table->val);
        if (ret) {
            PAL_LOGE(TAG, "寄存器写失败: 0x%04X=0x%02X, ret=%d",
                     table->reg, table->val, ret);
            return ret;
        }
    }
    return 0;
}

/* ================================================================
 *  分辨率表
 * ================================================================ */

static imx219_res_info_t s_res_table[] = {
    [IMX219_RES_640x480]   = { 640,  480  },
    [IMX219_RES_1280x720]  = { 1280, 720  },
    [IMX219_RES_1920x1080] = { 1920, 1080 },
    [IMX219_RES_3280x2464] = { 3280, 2464 },
};

imx219_res_info_t imx219_res_info(imx219_resolution_t res)
{
    if (res < sizeof(s_res_table) / sizeof(s_res_table[0]))
        return s_res_table[res];
    return (imx219_res_info_t){ .width = 1920, .height = 1080 };
}

/* ================================================================
 *  XCLK 生成（如果模块无板载晶振）
 * ================================================================ */

static int imx219_generate_xclk(int pin, uint32_t freq_hz)
{
    if (pin < 0) return 0;  /* 使用板载晶振 */

    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_2,
        .duty_resolution = LEDC_TIMER_2_BIT,
        .freq_hz         = freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&t);
    if (ret != ESP_OK) return ret;

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_6,
        .timer_sel  = LEDC_TIMER_2,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = pin,
        .duty       = 2,   /* 50% duty: 2/4 for 2-bit resolution */
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch);
    if (ret == ESP_OK) {
        PAL_LOGI(TAG, "XCLK: GPIO%d @ %lu Hz", pin, (unsigned long)freq_hz);
    }
    return ret;
}

/* ================================================================
 *  公开 API
 * ================================================================ */

int imx219_init(imx219_handle_t *handle, const imx219_config_t *cfg)
{
    if (!handle || !cfg || !cfg->i2c_dev) return -1;

    imx219_dev_t *d = calloc(1, sizeof(*d));
    if (!d) return -1;
    d->i2c_dev = cfg->i2c_dev;
    d->inited   = false;
    memcpy(&d->cfg, cfg, sizeof(*cfg));

    int ret;

    /* ---- 0. 生成 XCLK 24MHz（如果模块无板载晶振） ---- */
    ret = imx219_generate_xclk(cfg->xclk_pin, 24000000);
    if (ret) {
        PAL_LOGE(TAG, "XCLK 生成失败: %d", ret);
        free(d); return ret;
    }

    /* ---- 1. 读 ID 校验 ---- */
    uint16_t chip_id = 0;
    ret = imx219_read_id(d, &chip_id);
    if (ret) {
        PAL_LOGE(TAG, "读芯片 ID 失败: %d", ret);
        free(d); return ret;
    }
    PAL_LOGI(TAG, "芯片 ID: 0x%04X", chip_id);
    if (chip_id != IMX219_CHIP_ID) {
        PAL_LOGE(TAG, "ID 不匹配: 期望 0x%04X, 实际 0x%04X",
                 IMX219_CHIP_ID, chip_id);
        free(d); return -1;
    }

    /* ---- 2. LP-11 初始化序列（Linux 驱动关键步骤） ---- */
    /* 传感器上电后 MIPI 不在 LP-11 状态，需先进入 streaming 再回 standby */
    PAL_LOGI(TAG, "LP-11 初始化...");
    imx219_write_reg(d, REG_MODE_SELECT, MODE_STREAMING);
    osal_task_delay_ms(1);   /* >100us */
    imx219_write_reg(d, REG_MODE_SELECT, MODE_STANDBY);
    osal_task_delay_ms(1);   /* >100us */

    /* ---- 3. 软件复位 ---- */
    imx219_write_reg(d, REG_SW_RESET, 0x01);
    osal_task_delay_ms(10);

    /* ---- 4. 写入 COMMON 寄存器表 ---- */
    PAL_LOGI(TAG, "写入 COMMON 寄存器表...");
    ret = imx219_write_table(d, s_common_regs);
    if (ret) { free(d); return ret; }

    /* ---- 5. 写入 2-lane 配置 ---- */
    ret = imx219_write_table(d, s_2lane_regs);
    if (ret) { free(d); return ret; }

    /* ---- 6. 写入 MODE 配置 ---- */
    PAL_LOGI(TAG, "写入 MODE 配置 (1080p)...");
    ret = imx219_set_mode_1920x1080(d);
    if (ret) { free(d); return ret; }

    /* ---- 7. 进入 streaming ---- */
    osal_task_delay_ms(5);
    ret = imx219_write_reg(d, REG_MODE_SELECT, MODE_STREAMING);
    if (ret) { free(d); return ret; }
    osal_task_delay_ms(50);  /* 等待 MIPI 锁定 */

    /* ==== 诊断：回读关键寄存器 ==== */
    {
        uint8_t mode=0, lane=0, fmt_h=0, fmt_l=0, pll=0, oppxck=0;
        imx219_read_reg(d, REG_MODE_SELECT, &mode);
        imx219_read_reg(d, 0x0114, &lane);
        imx219_read_reg(d, 0x018C, &fmt_h);
        imx219_read_reg(d, 0x018D, &fmt_l);
        imx219_read_reg(d, 0x0307, &pll);
        imx219_read_reg(d, 0x0309, &oppxck);
        PAL_LOGI(TAG, "回读: MODE=0x%02X LANE=0x%02X FMT=0x%02X%02X PLL_MPY_L=0x%02X OPPXCK=0x%02X",
                 mode, lane, fmt_h, fmt_l, pll, oppxck);
    }

    d->inited = true;
    *handle = (imx219_handle_t)d;

    imx219_res_info_t info = imx219_res_info(d->cfg.resolution);
    PAL_LOGI(TAG, "初始化完成: %dx%d", info.width, info.height);
    return 0;
}

int imx219_deinit(imx219_handle_t handle)
{
    imx219_dev_t *d = (imx219_dev_t *)handle;
    if (!d || !d->inited) return -1;
    imx219_write_reg(handle, REG_MODE_SELECT, MODE_STANDBY);
    free(d);
    return 0;
}

int imx219_read_id(imx219_handle_t handle, uint16_t *id)
{
    uint8_t hi = 0, lo = 0;
    int ret;
    ret  = imx219_read_reg(handle, REG_CHIP_ID_H, &hi);
    ret |= imx219_read_reg(handle, REG_CHIP_ID_L, &lo);
    if (ret) return ret;
    *id = ((uint16_t)hi << 8) | lo;
    return 0;
}

int imx219_set_exposure(imx219_handle_t handle, uint32_t exposure_lines)
{
    int ret;
    ret  = imx219_write_reg(handle, 0x015A, (exposure_lines >> 8) & 0xFF);
    ret |= imx219_write_reg(handle, 0x015B, exposure_lines & 0xFF);
    return ret;
}

int imx219_set_gain(imx219_handle_t handle, uint16_t gain)
{
    return imx219_write_reg(handle, 0x0157, (gain >> 8) & 0xFF);
}
