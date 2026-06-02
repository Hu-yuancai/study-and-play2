/*
 * tetris_game.h - 俄罗斯方块游戏模块
 * 竖屏 10xN 网格, 左右移动/旋转/下落/硬降, 消行得分
 */
#ifndef TETRIS_GAME_H
#define TETRIS_GAME_H

#include <Arduino.h>
#include "config.h"

void tetrisGameInit();
void tetrisGameLoop(uint8_t key);

#endif
