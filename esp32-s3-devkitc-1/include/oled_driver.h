/*
 * oled_driver.h - OLED (SSD1306) I2C驱动, 14表情 + 眨眼 + 边框文字
 */
#ifndef OLED_DRIVER_H
#define OLED_DRIVER_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"

void initOLED();
void oledClear();
void oledRefresh();
void oledDrawString(int x, int y, const char* str);
void oledDrawBitmap(int x, int y, int w, int h, const uint8_t* bitmap);
void oledShowWelcome();
void oledShowFace(uint8_t faceType);
void oledShowPhanion();
void oledShowPhanion2();
void oledShowPhanion3();
void oledShowMydei1();
void oledShowMydei2();
void oledShowMydei3();
void oledShowConflict();
void oledShowBlinkingFace(uint8_t faceA, uint8_t faceB, uint32_t interval);
void oledDrawBorderText(const char* top, const char* bottom);
void oledShowScore(int score);

// 动态表情包: 播放第 group 组动画(分镜按 interval 毫秒循环切换)
void oledPlayEmojiAnim(uint8_t group, uint32_t interval);
// 循环播放所有表情组(每组停留 groupMs 毫秒, 组内分镜按 frameMs 切换)
void oledPlayEmojiIdle(uint32_t frameMs, uint32_t groupMs);
uint8_t oledEmojiGroupCount();

U8G2* getOLED();

// 14 表情类型
#define FACE_PIANO_1     0   // 按键1
#define FACE_PIANO_2     1   // 按键2
#define FACE_PIANO_4     2   // 按键4
#define FACE_PIANO_5     3   // 按键5
#define FACE_PIANO_COMBO 4   // combo≥10
#define FACE_PIANO_MISS  5   // miss

#define FACE_STUDY_A     6   // 学习倒计时眨眼A
#define FACE_STUDY_B     7   // 学习倒计时眨眼B

#define FACE_PAUSE_A     8   // 暂停/提前结束眨眼A
#define FACE_PAUSE_B     9   // 暂停/提前结束眨眼B

#define FACE_DONE_A      10  // 学习完成眨眼A
#define FACE_DONE_B      11  // 学习完成眨眼B

#define FACE_IDLE_A      12  // 菜单待机眨眼A
#define FACE_IDLE_B      13  // 菜单待机眨眼B

// 向后兼容别名
#define FACE_HAPPY  FACE_PIANO_COMBO
#define FACE_CHEER  FACE_PIANO_1
#define FACE_FOCUS  FACE_STUDY_A
#define FACE_DONE   FACE_DONE_A
#define FACE_SAD    FACE_PIANO_MISS

#define FACE_COUNT  14

#endif
