/*
 * oled_driver.h - OLED (SSD1306) I2C驱动
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
void oledShowScore(int score);
U8G2* getOLED();

// 表情类型
#define FACE_HAPPY    0
#define FACE_CHEER    1
#define FACE_FOCUS    2
#define FACE_DONE     3
#define FACE_SAD      4

#endif
