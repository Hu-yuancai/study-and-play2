/*
 * lcd_driver.cpp - LCD ILI9341 (320x240) SPI驱动 + GFXcanvas16帧缓冲 + 颜色支持
 */
#include "lcd_driver.h"
#include <SPI.h>

static Adafruit_ILI9341 tft(LCD_CS_PIN, LCD_DC_PIN, LCD_RST_PIN);
static GFXcanvas16 canvas(LCD_WIDTH, LCD_HEIGHT);
static uint16_t currentTextColor = COLOR_WHITE;
static uint16_t currentBgColor   = COLOR_BLACK;

void initLCD() {
  SPI.begin(LCD_SCLK_PIN, LCD_MISO_PIN, LCD_MOSI_PIN, LCD_CS_PIN);
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(COLOR_BLACK);
  canvas.fillScreen(COLOR_BLACK);
  canvas.setTextColor(COLOR_WHITE, COLOR_BLACK);
  canvas.setTextSize(1);
}

void lcdClear(uint16_t color) {
  canvas.fillScreen(color);
  currentBgColor = color;
}

void lcdRefresh() {
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), LCD_WIDTH, LCD_HEIGHT);
}

void lcdSetTextColor(uint16_t fg, uint16_t bg) {
  currentTextColor = fg;
  currentBgColor = bg;
  canvas.setTextColor(fg, bg);
}

void lcdDrawString(int x, int y, const char* str, uint8_t size) {
  canvas.setTextSize(size);
  canvas.setCursor(x, y);
  canvas.print(str);
}

void lcdDrawText(int x, int y, const char* str, uint8_t size) {
  lcdDrawString(x, y, str, size);
}

void lcdDrawPixel(int x, int y, uint16_t color) {
  canvas.drawPixel(x, y, color);
}

void lcdDrawRect(int x, int y, int w, int h, uint16_t color) {
  canvas.drawRect(x, y, w, h, color);
}

void lcdFillRect(int x, int y, int w, int h, uint16_t color) {
  canvas.fillRect(x, y, w, h, color);
}

void lcdDrawLine(int x0, int y0, int x1, int y1, uint16_t color) {
  canvas.drawLine(x0, y0, x1, y1, color);
}

void lcdDrawBitmap(int x, int y, int w, int h, const uint8_t* bitmap, uint16_t color) {
  canvas.drawBitmap(x, y, bitmap, w, h, color);
}

void lcdFillScreen(uint16_t color) {
  canvas.fillScreen(color);
}

GFXcanvas16* getLCD() {
  return &canvas;
}
