/**
 * @file    main.c
 * @brief   业务应用
 * 
 * @author  xLumina
 * @version 2.0
 */

#include <stdio.h>
#include <string.h>
#include "pal_log.h"

#include "dal_pir.h"
#include "osal_task.h"

static const char *TAG = "main";

/* 系统状态枚举 */
typedef enum system_state_t {
    SYS_STATE_DEEP_SLEEP,     // 深度休眠态：仅 PIR 中断工作，极低功耗
    SYS_STATE_IDLE,           // 待机/缓冲态：PIR 触发后等待确认，或人刚离开时的倒计时缓冲
    SYS_STATE_WORKING,        // 核心工作态：摄像头采集、AI 鉴权、UI 交互全开
    SYS_STATE_RELAY_OPEN,     // 开门执行态：继电器吸合，倒计时准备关门
    SYS_STATE_ERROR           // 故障态：外设异常或看门狗复位前的安全状态
} system_state_t;

// /**
//  * @brief 唤醒系统
//  */
// void wake_up_system(void)
// {
    
// }

// /**
//  * @brief 系统休眠
//  */
// void suspend_all_business_tasks()
// {

// }
 

// void system_manager_task(void *pvParameters) {
//     uint32_t ulNotifyValue;
//     uint16_t xLastActiveTime = 0;
//     const uint16_t xSleepDelay = pdMS_TO_TICKS(15000);

//     for (;;) {
//         // 阻塞等待事件，最多等 1 秒用于检查超时
//         if (xTaskNotifyWait(0, 0xFFFFFFFF, &ulNotifyValue, pdMS_TO_TICKS(1000)) == pdTRUE) {
//             xLastActiveTime = xTaskGetTickCount(); // 刷新活跃时间
            
//             // 根据收到的事件类型进行状态跳转
//             switch (ulNotifyValue) {
//                 case EVT_PIR_RISING:  // 人靠近
//                     if (current_state == SYS_STATE_DEEP_SLEEP || current_state == SYS_STATE_IDLE) {
//                         Wake_Up_System();
//                         current_state = SYS_STATE_WORKING;
//                     }
//                     break;
                    
//                 case EVT_AUTH_SUCCESS: // 鉴权成功
//                     if (current_state == SYS_STATE_WORKING) {
//                         Open_Relay();
//                         current_state = SYS_STATE_RELAY_OPEN;
//                     }
//                     break;
                    
//                 case EVT_RELAY_CLOSED: // 门已关上
//                     if (current_state == SYS_STATE_RELAY_OPEN) {
//                         current_state = SYS_STATE_IDLE; // 进入休眠缓冲期
//                     }
//                     break;
//             }
//         }

//         // 【超时检查机制】
//         // 只有在 IDLE 态，且超过 15 秒没有新事件，才真正休眠
//         if (current_state == SYS_STATE_IDLE && 
//             ((xTaskGetTickCount() - xLastActiveTime) >= xSleepDelay)) {
//             Suspend_All_Business_Tasks();
//             current_state = SYS_STATE_DEEP_SLEEP;
//         }
//     }
// }


static dal_pir_handle_t dal_pir_handle = NULL;
volatile system_state_t current_state = SYS_STATE_DEEP_SLEEP;

/**
 * @brief 人体红外感应中断回调 
 */
void pir_callback(dal_pir_state_t state, void *arg)
{
    switch (state) {
        case DAL_PIR_STATE_IDLE:
            current_state = SYS_STATE_DEEP_SLEEP;
            break;
        case DAL_PIR_STATE_MOTION:
            current_state = SYS_STATE_IDLE;
            break;           
        
        default:
            break;
    }
}

/**
 * @brief 硬件初始化
 */
void hardware_init(void)
{
    int ret;
    /* 人体红外感应模块初始化 */
    const dal_pir_config_t pir_cfg = {
      .gpio_pin = 1,
      .pull_down = true,
    };
    ret = dal_pir_init(&dal_pir_handle, &pir_cfg);
    if (ret != 0) {
        PAL_LOGE(TAG, "人体红外感应模块初始化失败");
    }
    /* 注册中断回调 */
    ret = dal_pir_set_callback(dal_pir_handle, pir_callback, NULL);
    if (ret != 0) {
        PAL_LOGE(TAG, "中断注册失败");
    }
    ret = dal_pir_enable(dal_pir_handle);
    if (ret != 0) {
        PAL_LOGE(TAG, "中断使能失败");
    }

}

void app_main(void)
{
    /* ----- 硬件初始化 ----- */
    hardware_init();
    /* ----- 业务运行 ----- */
    while (1) {
        switch (current_state) {
            case SYS_STATE_DEEP_SLEEP:
                PAL_LOGI(TAG, "休眠");
                break;
            case SYS_STATE_IDLE:
                PAL_LOGI(TAG, "空闲");
                break;
            default:
                break;
        }
        osal_task_delay_ms(20);
    }
    
   
}
