/*
 * minesweeper.h — 扫雷游戏模块
 * 12×12 网格 (可调 8-16), 键盘操作, OLED 表情联动
 */
#ifndef MINESWEEPER_H
#define MINESWEEPER_H

#include <Arduino.h>

void minesweeperInit();          // 初始化/重置游戏
void minesweeperLoop(uint8_t key); // 每帧调用

#endif
