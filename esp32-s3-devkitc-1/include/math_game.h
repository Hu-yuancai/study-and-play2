/*
 * math_game.h - 数学速算闯关模块
 * 主屏显示算式, 数字键输入答案, 限时计分
 */
#ifndef MATH_GAME_H
#define MATH_GAME_H

#include <Arduino.h>
#include "config.h"

void mathGameInit();
void mathGameLoop(uint8_t key);

#endif
