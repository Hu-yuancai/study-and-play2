/*
 * memory_game.cpp - 记忆翻牌 (配对消除)
 * 4x4=16张牌=8对, 牌面是字母A-H. 翻开两张相同则配对保留, 不同则盖回.
 * 方向键(A/B/C/D 或 2/8/4/6)移光标; 0/# 翻牌; * 返回. 全部配对即胜.
 */
#include "memory_game.h"
#include "game_common.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "audio.h"
#include "keyboard.h"
#include <Arduino.h>

#define GN     4                 // 4x4
#define NCARD  (GN*GN)           // 16
#define CARD   52                // 牌尺寸
#define GAP    6
#define BX     8                 // 牌阵左上
#define BY     56

static struct {
  uint8_t face[NCARD];     // 每张牌的字母索引 0-7 (成对出现)
  bool    revealed[NCARD]; // 已永久翻开(配对成功)
  int     cx, cy;          // 光标
  int     first;           // 第一张翻开的牌索引(-1无)
  int     pairs;           // 已配对数
  int     moves;           // 翻牌次数(步数)
  bool    gameOver;
  bool    waiting;         // 翻错两张后等待盖回
  int     showA, showB;    // 当前临时翻开的两张
  uint32_t waitStart;
} G;

static const char FACECH[8] = {'A','B','C','D','E','F','G','H'};

static void shuffle() {
  // 8对字母填入, 洗牌
  for (int i = 0; i < NCARD; i++) G.face[i] = i / 2;   // 0,0,1,1,...,7,7
  for (int i = NCARD - 1; i > 0; i--) {
    int j = random(i + 1);
    uint8_t t = G.face[i]; G.face[i] = G.face[j]; G.face[j] = t;
  }
}
// PLACEHOLDER_MEM1
// 某张牌当前是否显示牌面(已配对 或 是临时翻开的那两张之一)
static bool isShown(int idx) {
  return G.revealed[idx] || idx == G.showA || idx == G.showB;
}

static void render() {
  lcdClear(COLOR_DARKBG);
  // HUD
  lcdFillRect(0, 0, LCD_WIDTH, 46, COLOR_NAVY);
  lcdSetTextColor(COLOR_CYAN, COLOR_NAVY);
  lcdDrawString(8, 8, "Memory", 2);
  char buf[24];
  snprintf(buf, sizeof(buf), "Pairs:%d/8", G.pairs);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  lcdDrawString(110, 6, buf, 1);
  snprintf(buf, sizeof(buf), "Moves:%d", G.moves);
  lcdSetTextColor(COLOR_WHITE, COLOR_NAVY);
  lcdDrawString(110, 24, buf, 1);

  for (int i = 0; i < NCARD; i++) {
    int gx = i % GN, gy = i / GN;
    int x = BX + gx * (CARD + GAP);
    int y = BY + gy * (CARD + GAP);
    bool sel = (gx == G.cx && gy == G.cy);
    if (isShown(i)) {
      // 翻开: 浅底 + 字母
      lcdFillRect(x, y, CARD, CARD, G.revealed[i] ? 0x2D6B : COLOR_WHITE);
      char s[2] = { FACECH[G.face[i]], 0 };
      lcdSetTextColor(COLOR_NAVY, G.revealed[i] ? 0x2D6B : COLOR_WHITE);
      lcdDrawString(x + CARD/2 - 8, y + CARD/2 - 12, s, 3);
    } else {
      // 盖着: 深色卡背 + ?
      lcdFillRect(x, y, CARD, CARD, 0x1082);
      lcdSetTextColor(0x4208, 0x1082);
      lcdDrawString(x + CARD/2 - 5, y + CARD/2 - 8, "?", 2);
    }
    // 光标高亮边框
    if (sel) {
      lcdDrawRect(x, y, CARD, CARD, COLOR_GOLD);
      lcdDrawRect(x+1, y+1, CARD-2, CARD-2, COLOR_GOLD);
    }
  }
  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(20, LCD_HEIGHT - 12, "Arrows move  ENTER flip", 1);
  lcdRefresh();
}

void memoryGameInit() {
  randomSeed(micros());
  shuffle();
  for (int i = 0; i < NCARD; i++) G.revealed[i] = false;
  G.cx = G.cy = 0;
  G.first = -1;
  G.pairs = 0;
  G.moves = 0;
  G.gameOver = false;
  G.waiting = false;
  G.showA = G.showB = -1;
  oledDrawBorderText("Memory", "Match pairs!");
  oledShowBlinkingFace(FACE_IDLE_A, FACE_IDLE_B, 800);
  render();
}

static void drawWin() {
  lcdClear(COLOR_DARKBG);
  lcdFillRect(0, 100, LCD_WIDTH, 60, COLOR_NAVY);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  lcdDrawString(55, 115, "YOU WIN!", 3);
  char buf[24];
  snprintf(buf, sizeof(buf), "Moves: %d", G.moves);
  lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
  lcdDrawString(70, 180, buf, 2);
  lcdSetTextColor(COLOR_GRAY, COLOR_DARKBG);
  lcdDrawString(45, 230, "ENTER to retry", 2);
  lcdRefresh();
  oledDrawBorderText("Win!", buf);
  oledShowFace(FACE_PIANO_COMBO);
}
// PLACEHOLDER_MEM2
static void flipAt(int idx) {
  if (G.revealed[idx]) return;          // 已配对的不能翻
  if (idx == G.showA) return;           // 同一张不能翻两次

  if (G.first == -1) {
    // 翻开第一张
    G.first = idx;
    G.showA = idx;
    G.moves++;
    playNote(2);
    render();
  } else {
    // 翻开第二张
    G.showB = idx;
    G.moves++;
    render();
    if (G.face[idx] == G.face[G.first]) {
      // 配对成功
      G.revealed[idx] = true;
      G.revealed[G.first] = true;
      G.pairs++;
      G.first = -1; G.showA = G.showB = -1;
      playNote(7);
      oledShowFace(FACE_PIANO_COMBO);
      render();
      if (G.pairs >= 8) { G.gameOver = true; playSFX_success(); drawWin(); }
    } else {
      // 不配对: 进入等待, 稍后盖回
      G.waiting = true;
      G.waitStart = millis();
      playNote(1);
      oledShowFace(FACE_PIANO_MISS);
    }
  }
}

void memoryGameLoop(uint8_t key) {
  if (key == KEY_BACK) {
    currentMode = MODE_MENU;
    drawMenu();
    oledShowWelcome();
    return;
  }

  if (G.gameOver) {
    if (key == KEY_ENTER) memoryGameInit();
    return;
  }

  // 翻错两张后等待 800ms 自动盖回 (不依赖按键)
  if (G.waiting) {
    if (millis() - G.waitStart >= 800) {
      G.waiting = false;
      G.first = -1; G.showA = G.showB = -1;
      render();
    }
    return;   // 等待期间忽略按键
  }

  // 光标移动 (A/B/C/D 或 2/8/4/6)
  if (key == KEY_UP    || key == KEY_2) { if (G.cy > 0) G.cy--; render(); }
  if (key == KEY_DOWN  || key == KEY_8) { if (G.cy < GN-1) G.cy++; render(); }
  if (key == KEY_LEFT  || key == KEY_4) { if (G.cx > 0) G.cx--; render(); }
  if (key == KEY_RIGHT || key == KEY_6) { if (G.cx < GN-1) G.cx++; render(); }
  if (key == KEY_ENTER) flipAt(G.cy * GN + G.cx);
}


