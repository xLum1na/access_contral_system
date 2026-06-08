/**
 * @file    pal_mipi_csi.h
 * @brief   PAL MIPI CSI 模块 — 摄像头图像采集
 *
 * 封装 ESP-IDF esp_driver_cam 组件（esp_cam_ctlr + esp_cam_new_csi_ctlr），
 * 提供 CSI 控制器初始化、帧缓冲管理和阻塞帧捕获。
 *
 * 传感器初始化（如 IMX219 寄存器配置）由 DAL 层通过 pal_i2c 完成。
 * PAL 仅负责 CSI 主机端的数据流接收。
 *
 * 参考文档：ESP32-P4 TRM MIPI CSI 章节
 * @author  Access System Firmware Team
 * @version 2.0
 */

#ifndef PAL_MIPI_CSI_H
#define PAL_MIPI_CSI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief CSI 控制器不透明句柄 */
typedef void *pal_mipi_csi_handle_t;

/* ================================================================
 *  枚举类型
 * ================================================================ */

/** @brief 像素输入格式 */
typedef enum {
    PAL_CSI_COLOR_RAW8   = 0,   /**< RAW 8-bit */
    PAL_CSI_COLOR_RAW10  = 1,   /**< RAW 10-bit */
    PAL_CSI_COLOR_RGB565 = 2,   /**< RGB565 */
    PAL_CSI_COLOR_RGB888 = 3,   /**< RGB888 */
    PAL_CSI_COLOR_YUV422 = 4,   /**< YUV 4:2:2 */
} pal_mipi_csi_color_t;

/* ================================================================
 *  帧描述结构体
 * ================================================================ */

/**
 * @brief MIPI CSI 接收到的帧数据
 */
typedef struct {
    uint8_t  *buffer;        /**< 帧数据缓冲区指针 */
    size_t    buf_len;       /**< 缓冲区总长度（字节） */
    size_t    received_len;  /**< 实际接收的数据长度（字节） */
    uint32_t  timestamp_ms;  /**< 帧时间戳（相对系统启动） */
    uint32_t  seq_num;       /**< 帧序号 */
} pal_mipi_csi_frame_t;

/* ================================================================
 *  回调类型
 * ================================================================ */

/**
 * @brief MIPI CSI 帧回调函数（异步模式用）
 *
 * @param frame 收到的帧数据指针
 * @param arg   用户注册参数
 *
 * @note 回调运行在中断或高优先级任务上下文，仅做标志位操作。
 */
typedef void (*pal_mipi_csi_frame_cb_t)(pal_mipi_csi_frame_t *frame, void *arg);

/* ================================================================
 *  配置结构体
 * ================================================================ */

/**
 * @brief MIPI CSI 控制器初始化配置
 */
typedef struct {
    int                   ctlr_id;            /**< CSI 控制器编号（0 或 1） */
    uint16_t              h_res;              /**< 水平分辨率（像素/行） */
    uint16_t              v_res;              /**< 垂直分辨率（行/帧） */
    uint8_t               data_lane_num;      /**< 数据通道数（1 或 2） */
    int                   lane_bit_rate_mbps; /**< 通道比特率（Mbps） */
    pal_mipi_csi_color_t  input_color;        /**< 输入颜色格式（传感器输出） */
    pal_mipi_csi_color_t  output_color;       /**< 期望输出颜色格式 */
    int                   queue_items;        /**< 帧缓冲队列项数（建议 ≥ 2） */
    bool                  byte_swap_en;       /**< 是否使能字节交换 */
    bool                  bk_buffer_dis;      /**< 是否禁用备份缓冲 */

    /* ---- 帧缓冲分配 ---- */
    bool                  allocate_fb;        /**< 是否由 PAL 内部分配帧缓冲 */
    size_t                fb_size;            /**< 帧缓冲大小（allocate_fb=true 时自动计算） */
    void                 *external_fb;        /**< 外部提供的帧缓冲（allocate_fb=false 时使用） */

    /* ---- 异步回调（可选） ---- */
    pal_mipi_csi_frame_cb_t frame_cb;         /**< 帧完成回调（可选） */
    void                   *frame_cb_arg;     /**< 回调用户参数 */
} pal_mipi_csi_config_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

/**
 * @brief 初始化 MIPI CSI 控制器并分配帧缓冲
 *
 * 执行流程：CSI PHY LDO 供电 → 创建 CSI 控制器 → 分配帧缓冲 →
 * 使能控制器 → 注册事件回调。
 *
 * 调用后 CSI 处于就绪但未启动状态，需调用 pal_mipi_csi_start_stream()
 * 开始数据流。
 *
 * @param[out] handle 返回的 CSI 句柄
 * @param[in]  cfg    控制器配置
 * @return 0 成功，负数失败
 */
int pal_mipi_csi_init(pal_mipi_csi_handle_t *handle,
                      const pal_mipi_csi_config_t *cfg);

/**
 * @brief 反初始化 CSI 控制器，释放帧缓冲及所有资源
 *
 * @param handle CSI 句柄
 * @return 0 成功
 */
int pal_mipi_csi_deinit(pal_mipi_csi_handle_t handle);

/* ================================================================
 *  流控制 API
 * ================================================================ */

/**
 * @brief 启动 CSI 数据流接收
 *
 * @param handle CSI 句柄
 * @return 0 成功，负数失败
 */
int pal_mipi_csi_start_stream(pal_mipi_csi_handle_t handle);

/**
 * @brief 停止 CSI 数据流接收
 *
 * @param handle CSI 句柄
 * @return 0 成功
 */
int pal_mipi_csi_stop_stream(pal_mipi_csi_handle_t handle);

/* ================================================================
 *  帧获取 API
 * ================================================================ */

/**
 * @brief 获取一帧图像（阻塞）
 *
 * 内部调用 esp_cam_ctlr_receive() 等待 CSI 控制器接收完一帧数据。
 * 帧数据被写入初始化时分配的帧缓冲中。
 *
 * @param handle      CSI 句柄
 * @param[out] frame  帧数据（buffer 指向内部分配的帧缓冲）
 * @param timeout_ms  超时时间（ms），-1 = 永久等待
 * @return 0 成功，负数超时或错误
 *
 * @note 调用者不可释放 frame->buffer，由 PAL 内部管理。
 */
int pal_mipi_csi_capture(pal_mipi_csi_handle_t handle,
                         pal_mipi_csi_frame_t *frame,
                         int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* PAL_MIPI_CSI_H */
