/*
 * game2048.h - 2048 数字合并游戏模块
 * 4x4 方格, 四方向滑动合并相同数字, 目标 2048
 */
#ifndef GAME2048_H
#define GAME2048_H

#include <Arduino.h>
#include "config.h"

void game2048Init();
void game2048Loop(uint8_t key);

#endif
