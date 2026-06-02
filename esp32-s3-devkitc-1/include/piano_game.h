#ifndef PIANO_GAME_H
#define PIANO_GAME_H

#include <Arduino.h>

// 公共接口
void pianoGameInit();                // 初始化游戏（重置状态，加载当前曲目）
void pianoGameLoop(uint8_t key);     // 主循环，每帧调用
void pianoGameNextSong();            // 切换到下一首曲目
void pianoGamePrevSong();            // 切换到上一首曲目

#endif