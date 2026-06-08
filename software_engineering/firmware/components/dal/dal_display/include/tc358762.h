/**
 * @file    tc358762.h
 * @brief   TC358762 MIPI DSI → RGB 桥接芯片驱动
 *
 * TC358762 寄存器通过 DSI Generic Long Write (DT=0x29) 配置，**不支持 I2C**。
 * 本驱动通过 PAL DSI Generic Write API 写入寄存器。
 *
 * 初始化必须在 DSI 总线和 DPI 面板已创建、ATtiny88 已释放桥复位之后进行。
 *
 * 参考：
 *   - 树莓派 7" DSI 屏验证文档
 *   - Linux 内核驱动: drivers/gpu/drm/bridge/tc358762.c
 * @author  Access System Firmware Team
 * @version 2.0
 */

#ifndef TC358762_H
#define TC358762_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *pal_mipi_dsi_handle_t;
typedef struct tc358762_dev *tc358762_handle_t;

/* ================================================================
 *  寄存器地址（16-bit）
 * ================================================================ */

/* DSI 配置 */
#define TC358762_REG_DSI_LANEENABLE    0x0100   /**< DSI 通道使能 */
#define TC358762_REG_CLRSIPOCOUNT      0x0160   /**< CLRSIPO 计数 */
#define TC358762_REG_ATMR              0x0168   /**< ATMR */
#define TC358762_REG_LPTXTIMECNT       0x0208   /**< LPX 超时计数 */

/* 系统 */
#define TC358762_REG_SYSCTRL           0x0460   /**< 系统控制 */
#define TC358762_REG_LCDCTRL           0x0478   /**< LCD 控制 */
#define TC358762_REG_SYSPMCTRL         0x047C   /**< 系统电源管理控制 */
#define TC358762_REG_PPI_STARTPPI      0x0480   /**< PPI 启动 */
#define TC358762_REG_DSI_STARTDSI      0x0484   /**< DSI 启动 */

/* 时序 */
#define TC358762_REG_HS_HBP            0x0380   /**< HSYNC + HBP */
#define TC358762_REG_HDISP_HFP         0x0384   /**< HDISP + HFP */
#define TC358762_REG_VS_VBP            0x0388   /**< VSYNC + VBP */
#define TC358762_REG_VDISP_VFP         0x038C   /**< VDISP + VFP */

/* ---- 寄存器位定义 ---- */
#define DSI_LANEENABLE_CLOCK           (1 << 0)
#define DSI_LANEENABLE_D0              (1 << 1)

#define SYSCTRL_SRST                   (1 << 0)
#define SYSCTRL_SLEEP                  (1 << 1)
/* bit 2-3: 保留 */
/* bit 10: DSI 启动, bit 12: PPI 启动（实际由独立寄存器控制） */

/* ================================================================
 *  配置结构体
 * ================================================================ */

typedef struct {
    uint16_t h_res;
    uint16_t v_res;
    uint16_t hsync_pulse_width;
    uint16_t hsync_back_porch;
    uint16_t hsync_front_porch;
    uint16_t vsync_pulse_width;
    uint16_t vsync_back_porch;
    uint16_t vsync_front_porch;
} tc358762_timing_t;

/**
 * @brief TC358762 初始化配置
 */
typedef struct {
    pal_mipi_dsi_handle_t dsi_handle;       /**< PAL DSI 句柄（用于 Generic Write） */
    uint8_t               vc;               /**< DSI 虚拟通道 */
    tc358762_timing_t     timing;           /**< 显示时序 */
} tc358762_config_t;

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 通过 DSI Generic Write 初始化 TC358762 寄存器
 *
 * @param[out] handle 设备句柄
 * @param[in]  cfg    配置参数
 * @return 0 成功
 */
int tc358762_init(tc358762_handle_t *handle, const tc358762_config_t *cfg);

/**
 * @brief 反初始化
 */
int tc358762_deinit(tc358762_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TC358762_H */
