/*
 * memory_game.h - 记忆翻牌模块
 * 4x4 牌阵, 翻开两张找相同配对, 锻炼记忆力
 */
#ifndef MEMORY_GAME_H
#define MEMORY_GAME_H

#include <Arduino.h>
#include "config.h"

void memoryGameInit();
void memoryGameLoop(uint8_t key);

#endif
