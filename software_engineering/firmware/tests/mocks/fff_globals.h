/**
 * @file    fff_globals.h
 * @brief   全局 FFF 定义文件
 *
 * 在测试项目中仅在一个 .c 文件中包含本文件（配合 #include "fff.h"），
 * 其余 .c 文件只 #include "fff.h" 获取 FAKE_VALUE_FUNC / RESET_FAKE 等声明。
 *
 * 用法：
 *   在 CMakeLists 中单独编译 fff_globals.c，其内容为：
 *     #include "fff.h"
 *     #include "fff_globals.h"
 *     DEFINE_FFF_GLOBALS;
 */

#ifndef FFF_GLOBALS_H
#define FFF_GLOBALS_H

/* 被主测试工程中的一个 .c 文件包含并展开 DEFINE_FFF_GLOBALS。
   此处为占位，实际代码在 tests/fff_globals.c 中。 */

#endif /* FFF_GLOBALS_H */
