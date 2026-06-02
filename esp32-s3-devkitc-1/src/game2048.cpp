/*
 * game2048.cpp - 2048 数字合并
 * 4x4 棋盘, 方向键滑动, 相同数字合并翻倍, 出现 2048 为胜, 无法移动为负
 * A/上 B/下 C/左 D/右 滑动; * 返回; 结束后 0/# 重开
 */
#include "game2048.h"
#include "game_common.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "audio.h"
#include "keyboard.h"
#include <Arduino.h>

#define N        4
#define TILE     52                 // 方块边长
#define GAP      6                  // 间隙
#define BOARD_W  (N * TILE + (N + 1) * GAP)   // 4*52+5*6 = 238
#define BX       1                  // 棋盘左上 x (240-238)/2≈1
#define BY       58                 // 棋盘左上 y (HUD下方)

static struct {
  int  grid[N][N];     // 0=空, 否则为数值(2,4,8,...)
  int  score;
  bool won;
  bool gameOver;
  bool keepGoing;      // 到2048后选择继续
} g2;

// 取某数值对应的方块颜色(RGB565)
static uint16_t tileColor(int v) {
  switch (v) {
    case 2:    return 0xEF7D; // 浅灰
    case 4:    return 0xEF3A;
    case 8:    return 0xFD00; // 橙
    case 16:   return 0xFCA0;
    case 32:   return 0xFB40; // 红橙
    case 64:   return 0xF9C0;
    case 128:  return 0xFEC0; // 黄
    case 256:  return 0xFEA0;
    case 512:  return 0xFE60;
    case 1024: return 0x07E0; // 绿
    case 2048: return 0x07FF; // 青
    default:   return 0x39E7; // 更大=深灰
  }
}

// 在随机空格生成一个新数(90% 是2, 10% 是4)
static void spawnTile() {
  int empty = 0;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
      if (g2.grid[y][x] == 0) empty++;
  if (empty == 0) return;
  int target = random(empty);
  int idx = 0;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
      if (g2.grid[y][x] == 0) {
        if (idx == target) { g2.grid[y][x] = (random(10) == 0) ? 4 : 2; return; }
        idx++;
      }
}
// PLACEHOLDER_G1
// 把一行4个数向左滑动合并, 返回是否有变化, 累加得分
static bool slideLine(int line[N], int &score) {
  int tmp[N] = {0,0,0,0};
  int idx = 0;
  bool changed = false;
  // 1) 压缩(去掉0)
  for (int i = 0; i < N; i++) if (line[i]) tmp[idx++] = line[i];
  // 2) 合并相邻相同
  for (int i = 0; i < N - 1; i++) {
    if (tmp[i] != 0 && tmp[i] == tmp[i+1]) {
      tmp[i] *= 2;
      score += tmp[i];
      tmp[i+1] = 0;
    }
  }
  // 3) 再压缩
  int out[N] = {0,0,0,0};
  idx = 0;
  for (int i = 0; i < N; i++) if (tmp[i]) out[idx++] = tmp[i];
  // 4) 比较是否变化, 写回
  for (int i = 0; i < N; i++) {
    if (line[i] != out[i]) changed = true;
    line[i] = out[i];
  }
  return changed;
}

// 按方向滑动整个棋盘 dir: 0上 1下 2左 3右
static bool move(int dir) {
  bool changed = false;
  int gained = 0;
  if (dir == 2 || dir == 3) {          // 左/右: 按行处理
    for (int y = 0; y < N; y++) {
      int line[N];
      for (int x = 0; x < N; x++)
        line[x] = (dir == 2) ? g2.grid[y][x] : g2.grid[y][N-1-x];
      if (slideLine(line, gained)) changed = true;
      for (int x = 0; x < N; x++) {
        if (dir == 2) g2.grid[y][x] = line[x];
        else          g2.grid[y][N-1-x] = line[x];
      }
    }
  } else {                              // 上/下: 按列处理
    for (int x = 0; x < N; x++) {
      int line[N];
      for (int y = 0; y < N; y++)
        line[y] = (dir == 0) ? g2.grid[y][x] : g2.grid[N-1-y][x];
      if (slideLine(line, gained)) changed = true;
      for (int y = 0; y < N; y++) {
        if (dir == 0) g2.grid[y][x] = line[y];
        else          g2.grid[N-1-y][x] = line[y];
      }
    }
  }
  g2.score += gained;
  return changed;
}

// 是否还有合法移动(有空格或相邻相同)
static bool canMove() {
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      if (g2.grid[y][x] == 0) return true;
      if (x < N-1 && g2.grid[y][x] == g2.grid[y][x+1]) return true;
      if (y < N-1 && g2.grid[y][x] == g2.grid[y+1][x]) return true;
    }
  return false;
}
// PLACEHOLDER_G2
static void render() {
  lcdClear(COLOR_DARKBG);
  // HUD
  lcdFillRect(0, 0, LCD_WIDTH, 50, COLOR_NAVY);
  lcdSetTextColor(COLOR_CYAN, COLOR_NAVY);
  lcdDrawString(8, 8, "2048", 2);
  char buf[24];
  snprintf(buf, sizeof(buf), "Score:%d", g2.score);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  lcdDrawString(110, 16, buf, 1);

  // 棋盘背景
  lcdFillRect(BX, BY, BOARD_W, BOARD_W, 0x4208);
  // 方块
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      int px = BX + GAP + x * (TILE + GAP);
      int py = BY + GAP + y * (TILE + GAP);
      int v = g2.grid[y][x];
      lcdFillRect(px, py, TILE, TILE, v ? tileColor(v) : 0x632C);
      if (v) {
        // 数字: 值越大字号越小, 居中
        uint8_t sz = (v < 100) ? 3 : (v < 1000) ? 2 : 2;
        char nb[8];
        snprintf(nb, sizeof(nb), "%d", v);
        int len = strlen(nb);
        int tw = len * 6 * sz;       // 估算文字宽
        int th = 8 * sz;
        int tx = px + (TILE - tw) / 2;
        int ty = py + (TILE - th) / 2;
        lcdSetTextColor(v <= 4 ? COLOR_NAVY : COLOR_BLACK, tileColor(v));
        lcdDrawString(tx, ty, nb, sz);
      }
    }
  lcdRefresh();
}

void game2048Init() {
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) g2.grid[y][x] = 0;
  g2.score = 0;
  g2.won = false;
  g2.gameOver = false;
  g2.keepGoing = false;
  randomSeed(micros());
  spawnTile();
  spawnTile();
  oledDrawBorderText("2048", "Slide & merge");
  oledShowBlinkingFace(FACE_IDLE_A, FACE_IDLE_B, 800);
  render();
}

static void checkWin() {
  if (g2.keepGoing) return;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
      if (g2.grid[y][x] >= 2048) { g2.won = true; return; }
}

static void drawBanner(const char* title, const char* sub) {
  lcdFillRect(0, 120, LCD_WIDTH, 70, COLOR_NAVY);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  lcdDrawString(40, 135, title, 3);
  lcdSetTextColor(COLOR_WHITE, COLOR_NAVY);
  lcdDrawString(30, 168, sub, 1);
  lcdRefresh();
}

void game2048Loop(uint8_t key) {
  if (key == KEY_BACK) {
    currentMode = MODE_MENU;
    drawMenu();
    oledShowWelcome();
    return;
  }

  if (g2.gameOver) {
    if (key == KEY_ENTER) { game2048Init(); return; }
    return;
  }
  if (g2.won) {
    // 胜利提示: ENTER 继续玩, BACK 已在上面处理
    if (key == KEY_ENTER) { g2.keepGoing = true; g2.won = false; render(); }
    return;
  }

  int dir = -1;
  if (key == KEY_UP    || key == KEY_2) dir = 0;
  if (key == KEY_DOWN  || key == KEY_8) dir = 1;
  if (key == KEY_LEFT  || key == KEY_4) dir = 2;
  if (key == KEY_RIGHT || key == KEY_6) dir = 3;
  if (dir < 0) return;

  if (move(dir)) {
    playNote(2);
    spawnTile();
    render();
    checkWin();
    if (g2.won) { oledShowFace(FACE_PIANO_COMBO); drawBanner("2048!", "ENTER=keep going"); return; }
    if (!canMove()) {
      g2.gameOver = true;
      playSFX_explode();
      oledShowFace(FACE_PIANO_MISS);
      drawBanner("GAME OVER", "ENTER to retry");
    }
  }
}


