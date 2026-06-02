/*
 * ESP32-S3 学习游戏一体机 - 主程序
 * 硬件: ESP32-S3-DevKitC-1 + LCD12864(ST7920) + OLED(SSD1306) + 4x4键盘 + LM386功放
 * 功能: 钢琴游戏 / 飞机大战 / 学习伴侣
 */

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <SPI.h>
#include "config.h"
#include "game_common.h"
#include "audio.h"
#include "keyboard.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "piano_game.h"
#include "plane_game.h"
#include "minesweeper.h"
#include "snake_game.h"
#include "tetris_game.h"
#include "game2048.h"
#include "study_buddy.h"
#include "vocab.h"
#include "math_game.h"
#include "memory_game.h"
#include "ghost_shell.h"
#include "safe_ram.h"

GameMode currentMode = MODE_MENU;
int menuSelection = 0;
static unsigned long lastMenuOled;

// ── 游戏限时(防沉迷): 累计连续游玩时间, 超过阈值提示并踢回菜单 ──
#define GAME_TIME_LIMIT_MS  (10UL * 60UL * 1000UL)  // 10分钟
static unsigned long gamePlayStart = 0;   // 本段游戏开始时刻(0=当前不在游戏中)

// 判断某模式是否算"游戏"(学习/菜单/背单词不计时)
static bool isGameMode(GameMode m) {
  return m == MODE_PIANO || m == MODE_PLANE || m == MODE_MINESWEEPER ||
         m == MODE_SNAKE || m == MODE_TETRIS || m == MODE_2048;
}

void setup() {
  Serial.begin(115200);
  // 原生USB(CDC)需等待枚举就绪, 否则最早的日志会丢失; 最多等2秒
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 2000) { delay(10); }
  delay(300);
  Serial.println();
  Serial.println("=== ESP32-S3 Learning Game Machine ===");

  safeRAMInit();   // Ghost Shell 沙盒内存 + 扫雷参数默认值
  Serial.println("[1] initAudio...");
  initAudio();
  // 不播开机提示音: 上电瞬间供电最紧张, 避免叠加蜂鸣器电流引发欠压
  Serial.println("[2] initKeyboard...");
  initKeyboard();
  Serial.println("[3] initLCD...");
  initLCD();
  Serial.println("[4] initOLED...");
  initOLED();
  Serial.println("[5] drawMenu...");
  drawMenu();
  oledShowWelcome();
  lastMenuOled = millis();
  Serial.println("[6] setup done, entering loop");
}

void handleMenu(uint8_t key) {
  // ── Konami 码: ↑↑↓↓←→←→ FIRE ENTER → 进入隐藏 Ghost Shell ──
  static const uint8_t konami[] = {KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN,
    KEY_LEFT, KEY_RIGHT, KEY_LEFT, KEY_RIGHT, KEY_FIRE, KEY_ENTER};
  static int konamiPos = 0;
  if (key != KEY_NONE) {
    if (key == konami[konamiPos]) {
      konamiPos++;
      if (konamiPos == 10) {
        konamiPos = 0;
        playTone(880, 40); delay(50);
        playTone(1100, 40); delay(50);
        playTone(1400, 60);
        currentMode = MODE_SHELL;
        shellInit();
        return;
      }
    } else {
      konamiPos = (key == konami[0]) ? 1 : 0;
    }
  }

  if (key == KEY_UP) {
    menuSelection = (menuSelection + 9) % 10;
    drawMenu();
  } else if (key == KEY_DOWN) {
    menuSelection = (menuSelection + 1) % 10;
    drawMenu();
  } else if (key == KEY_ENTER) {
    switch (menuSelection) {
      case 0:
        currentMode = MODE_PIANO;
        pianoGameInit();
        break;
      case 1:
        currentMode = MODE_PLANE;
        planeGameInit();
        break;
      case 2:
        currentMode = MODE_MINESWEEPER;
        minesweeperInit();
        break;
      case 3:
        currentMode = MODE_SNAKE;
        snakeGameInit();
        break;
      case 4:
        currentMode = MODE_TETRIS;
        tetrisGameInit();
        break;
      case 5:
        currentMode = MODE_2048;
        game2048Init();
        break;
      case 6:
        currentMode = MODE_STUDY;
        studyBuddyInit();
        break;
      case 7:
        currentMode = MODE_VOCAB;
        vocabInit();
        break;
      case 8:
        currentMode = MODE_MATH;
        mathGameInit();
        break;
      case 9:
        currentMode = MODE_MEMORY;
        memoryGameInit();
        break;
    }
  }
}

void loop() {
  uint8_t key = scanKeyboard();

  audioUpdate();  // 推进非阻塞音效

  // ── 游戏限时检查(防沉迷) ──
  if (isGameMode(currentMode)) {
    if (gamePlayStart == 0) gamePlayStart = millis();   // 刚进入游戏, 开始计时
    if (millis() - gamePlayStart >= GAME_TIME_LIMIT_MS) {
      // 超时: 显示休息提示, 踢回主菜单
      gamePlayStart = 0;
      lcdClear(COLOR_DARKBG);
      lcdFillRect(0, 110, LCD_WIDTH, 90, COLOR_NAVY);
      lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
      lcdDrawString(35, 125, "Time's up!", 3);
      lcdSetTextColor(COLOR_WHITE, COLOR_NAVY);
      lcdDrawString(30, 165, "Take a break, study!", 1);
      lcdRefresh();
      oledDrawBorderText("Rest time", "Go study!");
      oledShowFace(FACE_PIANO_MISS);
      playSFX_success();
      delay(2500);
      currentMode = MODE_MENU;
      menuSelection = 0;
      drawMenu();
      oledShowWelcome();
      return;
    }
  } else {
    gamePlayStart = 0;   // 不在游戏中, 重置计时(学习/菜单/背单词不计入)
  }

  switch (currentMode) {
    case MODE_MENU:
      handleMenu(key);
      // 菜单待机: 副屏循环播放动态表情包 (每帧600ms, 每组4.5秒切换)
      if (millis() - lastMenuOled > 500) {
        oledPlayEmojiIdle(600, 4500);
        lastMenuOled = millis();
      }
      break;
    case MODE_PIANO:
      pianoGameLoop(key);
      break;
    case MODE_PLANE:
      planeGameLoop(key);
      break;
    case MODE_MINESWEEPER:
      minesweeperLoop(key);
      break;
    case MODE_SNAKE:
      snakeGameLoop(key);
      break;
    case MODE_TETRIS:
      tetrisGameLoop(key);
      break;
    case MODE_2048:
      game2048Loop(key);
      break;
    case MODE_STUDY:
      studyBuddyLoop(key);
      break;
    case MODE_VOCAB:
      vocabLoop(key);
      break;
    case MODE_MATH:
      mathGameLoop(key);
      break;
    case MODE_MEMORY:
      memoryGameLoop(key);
      break;
    case MODE_SHELL:
      shellLoop(key);
      break;
  }

  delay(16);
}

void drawMenu() {
  lcdClear(COLOR_DARKBG);

  // 顶部标题栏 (黄底紫字)
  lcdFillRect(0, 0, LCD_WIDTH, 42, COLOR_YELLOW);
  lcdSetTextColor(0x8010, COLOR_YELLOW);
  lcdDrawString(8, 12, "Learning Game", 2);

  // 10 个菜单项 (前6游戏 + 后4学习), 滚动窗口显示
  const char* items[] = {"Piano Game", "Plane Battle", "Minesweeper",
                         "Snake", "Tetris", "2048",
                         "Study Buddy", "Vocabulary", "Math Drill", "Memory"};
  const uint16_t accent[] = {COLOR_CYAN, COLOR_GREEN, COLOR_ORANGE, 0x07E0,
                             0xFD20, 0xFFE0, COLOR_GOLD, 0x07FF, 0xFB56, 0xF81F};
  const int TOTAL = 10;
  const int VISIBLE = 7;          // 一屏显示7项

  // 计算滚动起点, 保证选中项可见
  int top = menuSelection - VISIBLE / 2;
  if (top < 0) top = 0;
  if (top > TOTAL - VISIBLE) top = TOTAL - VISIBLE;

  for (int row = 0; row < VISIBLE; row++) {
    int i = top + row;
    bool sel = (menuSelection == i);
    int y = 48 + row * 32;
    int rowH = 28;
    // 学习类(下标>=6)加一个小标记区分
    bool isStudy = (i >= 6);

    if (sel) {
      lcdFillRect(8, y, LCD_WIDTH - 16, rowH, 0x1082);
      lcdFillRect(8, y, 5, rowH, accent[i]);
      lcdSetTextColor(0xFE19, 0x1082);
    } else {
      lcdFillRect(8, y, 5, rowH, accent[i]);
      lcdSetTextColor(isStudy ? 0x07FF : 0x9CD3, COLOR_DARKBG);
    }
    lcdDrawString(24, y + 6, items[i], 2);
  }

  // 滚动指示 (上/下还有更多)
  lcdSetTextColor(0x8410, COLOR_DARKBG);
  if (top > 0) lcdDrawString(LCD_WIDTH - 18, 48, "^", 1);
  if (top < TOTAL - VISIBLE) lcdDrawString(LCD_WIDTH - 18, 48 + (VISIBLE-1)*32 + 6, "v", 1);

  // 底部
  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(35, LCD_HEIGHT - 10, "UP/DOWN  ENTER", 1);

  lcdRefresh();
}
