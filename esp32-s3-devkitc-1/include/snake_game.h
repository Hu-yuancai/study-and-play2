/*
 * snake_game.h - 贪吃蛇游戏模块
 * 方向键控制, 吃果子变长, 撞墙/撞自己结束
 */
#ifndef SNAKE_GAME_H
#define SNAKE_GAME_H

#include <Arduino.h>
#include "config.h"

void snakeGameInit();
void snakeGameLoop(uint8_t key);

#endif
