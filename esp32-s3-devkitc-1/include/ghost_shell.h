/*
 * ghost_shell.h — 隐藏调试菜单 (LCD+键盘操控, 无串口)
 * 主菜单按 KEY_7 进入, 不上屏显入口.
 */
#ifndef GHOST_SHELL_H
#define GHOST_SHELL_H
#include <Arduino.h>

void shellInit();
void shellLoop(uint8_t key);

#endif
