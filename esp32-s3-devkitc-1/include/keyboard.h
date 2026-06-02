/*
 * keyboard.h - 4x4矩阵键盘驱动
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <Arduino.h>
#include "config.h"

void initKeyboard();
uint8_t scanKeyboard();
bool isKeyPressed(uint8_t key);

// ===== 按键事件队列 (环形缓冲, 防漏键; 当前主循环未启用, 备用) =====
#define KEY_QUEUE_SIZE 8
void keyQueuePush(uint8_t key);
uint8_t keyQueuePop();
bool keyQueueAvailable();
void keyQueueFlush();

#endif
