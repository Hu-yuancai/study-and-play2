/*
 * plane_game.h - 飞机大战模块
 */
#ifndef PLANE_GAME_H
#define PLANE_GAME_H

#include <Arduino.h>
#include "config.h"

void planeGameInit();
void planeGameLoop(uint8_t key);

#endif
