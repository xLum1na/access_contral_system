/**
 * @file    fff_globals.c
 * @brief   FFF 全局定义 — 所有 FAKE_* 函数体和全局变量在此编译
 *
 * 一个测试工程中只能有一个 .c 文件定义 FFF_MOCK_DEFINITIONS。
 * 包含本文件即可得到所有 mock 函数的定义。
 */

#define FFF_MOCK_DEFINITIONS
#include "fff.h"
DEFINE_FFF_GLOBALS;

/* 按需包含各模块 mock 头文件，Fake 函数体在此编译 */
#include "driver/gpio.h"
