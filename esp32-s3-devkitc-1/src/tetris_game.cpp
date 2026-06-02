/*
 * tetris_game.cpp - 俄罗斯方块
 * 网格 10x14, 7种方块, 左右移/旋转/软降/硬降, 满行消除计分升级
 * C/左 D/右 移动; A/上 旋转; B/下 软降; 9/FIRE 硬降; 0/# 暂停; * 返回
 */
#include "tetris_game.h"
#include "game_common.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "audio.h"
#include "keyboard.h"
#include <Arduino.h>

#define COLS    10
#define ROWS    14
#define CELL    20
#define X_OFF   20                 // 棋盘左边距: (240-200)/2
#define HUD_H   40                 // 顶部计分栏

// 7种方块, 每种4x4格, 用4个旋转态预存太占空间; 改用相对坐标+旋转计算。
// 这里用每个方块的4个格子相对坐标(基于4x4框), 旋转用矩阵变换。
// 方块类型颜色(RGB565)
static const uint16_t pieceColors[7] = {
  0x07FF, // I 青
  0xFFE0, // O 黄
  0xF81F, // T 品红
  0x07E0, // S 绿
  0xF800, // Z 红
  0x001F, // J 蓝
  0xFD20  // L 橙
};

// 每种方块在4x4内的初始4格坐标 {x,y}
static const int8_t pieceCells[7][4][2] = {
  {{0,1},{1,1},{2,1},{3,1}}, // I
  {{1,0},{2,0},{1,1},{2,1}}, // O
  {{1,0},{0,1},{1,1},{2,1}}, // T
  {{1,0},{2,0},{0,1},{1,1}}, // S
  {{0,0},{1,0},{1,1},{2,1}}, // Z
  {{0,0},{0,1},{1,1},{2,1}}, // J
  {{2,0},{0,1},{1,1},{2,1}}  // L
};

static struct {
  uint8_t  board[ROWS][COLS];   // 0空, 否则=颜色索引+1
  int      curType;
  int      curRot;
  int      curX, curY;          // 当前方块在4x4框的左上角在棋盘的位置
  int      nextType;
  int      score;
  int      lines;
  int      level;
  bool     gameOver;
  bool     paused;
  uint32_t lastDrop;
  uint32_t dropInterval;
} tet;

// 取方块第idx格在rot旋转下的棋盘坐标(相对curX,curY)
static void cellPos(int type, int rot, int idx, int &ox, int &oy) {
  int x = pieceCells[type][idx][0];
  int y = pieceCells[type][idx][1];
  // 以4x4框中心(1.5,1.5)旋转rot次90度
  for (int r = 0; r < rot; r++) {
    int nx = 3 - y;   // 顺时针: (x,y)->(3-y, x) 在0..3范围
    int ny = x;
    x = nx; y = ny;
  }
  ox = x; oy = y;
}
// PLACEHOLDER_T1
// 检测方块在 (px,py,rot) 是否合法(不出界、不重叠)
static bool valid(int type, int rot, int px, int py) {
  for (int i = 0; i < 4; i++) {
    int ox, oy;
    cellPos(type, rot, i, ox, oy);
    int bx = px + ox;
    int by = py + oy;
    if (bx < 0 || bx >= COLS || by >= ROWS) return false;
    if (by >= 0 && tet.board[by][bx]) return false;
  }
  return true;
}

// 把当前方块固定到棋盘
static void lockPiece() {
  for (int i = 0; i < 4; i++) {
    int ox, oy;
    cellPos(tet.curType, tet.curRot, i, ox, oy);
    int bx = tet.curX + ox;
    int by = tet.curY + oy;
    if (by >= 0 && by < ROWS && bx >= 0 && bx < COLS)
      tet.board[by][bx] = tet.curType + 1;
  }
}

// 消除满行, 返回消除行数
static int clearLines() {
  int cleared = 0;
  for (int y = ROWS - 1; y >= 0; y--) {
    bool full = true;
    for (int x = 0; x < COLS; x++) if (!tet.board[y][x]) { full = false; break; }
    if (full) {
      cleared++;
      // 上面所有行下移一行
      for (int yy = y; yy > 0; yy--)
        for (int x = 0; x < COLS; x++) tet.board[yy][x] = tet.board[yy-1][x];
      for (int x = 0; x < COLS; x++) tet.board[0][x] = 0;
      y++; // 重新检查这一行
    }
  }
  return cleared;
}

static void spawnPiece() {
  tet.curType = tet.nextType;
  tet.nextType = random(7);
  tet.curRot = 0;
  tet.curX = 3;
  tet.curY = 0;
  if (!valid(tet.curType, tet.curRot, tet.curX, tet.curY)) {
    tet.gameOver = true;
    playSFX_explode();
  }
}
// PLACEHOLDER_T2
static void drawBlock(int bx, int by, uint16_t color) {
  int px = X_OFF + bx * CELL;
  int py = HUD_H + by * CELL;
  lcdFillRect(px + 1, py + 1, CELL - 2, CELL - 2, color);
}

static void render() {
  lcdClear(COLOR_BLACK);
  // HUD
  lcdFillRect(0, 0, LCD_WIDTH, HUD_H, COLOR_NAVY);
  lcdSetTextColor(COLOR_CYAN, COLOR_NAVY);
  lcdDrawString(6, 4, "TETRIS", 1);
  char buf[24];
  snprintf(buf, sizeof(buf), "S:%d", tet.score);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  lcdDrawString(6, 22, buf, 1);
  snprintf(buf, sizeof(buf), "Lv:%d L:%d", tet.level, tet.lines);
  lcdSetTextColor(COLOR_WHITE, COLOR_NAVY);
  lcdDrawString(90, 22, buf, 1);
  // 棋盘边框
  lcdDrawRect(X_OFF - 1, HUD_H - 1, COLS * CELL + 2, ROWS * CELL + 2, COLOR_GRAY);
  // 已固定方块
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++)
      if (tet.board[y][x]) drawBlock(x, y, pieceColors[tet.board[y][x] - 1]);
  // 当前下落方块
  if (!tet.gameOver) {
    for (int i = 0; i < 4; i++) {
      int ox, oy; cellPos(tet.curType, tet.curRot, i, ox, oy);
      int bx = tet.curX + ox, by = tet.curY + oy;
      if (by >= 0) drawBlock(bx, by, pieceColors[tet.curType]);
    }
  }
  lcdRefresh();
}

void tetrisGameInit() {
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++) tet.board[y][x] = 0;
  tet.score = 0;
  tet.lines = 0;
  tet.level = 1;
  tet.gameOver = false;
  tet.paused = false;
  tet.dropInterval = 600;
  tet.lastDrop = millis();
  randomSeed(micros());
  tet.nextType = random(7);
  spawnPiece();
  oledDrawBorderText("Tetris", "Up=rotate");
  oledShowBlinkingFace(FACE_IDLE_A, FACE_IDLE_B, 800);
  render();
}
// PLACEHOLDER_T3
// 方块落定后: 消行计分、升级、生成新块
static void afterLock() {
  lockPiece();
  int n = clearLines();
  if (n > 0) {
    static const int lineScore[5] = {0, 100, 300, 500, 800};
    tet.score += lineScore[n] * tet.level;
    tet.lines += n;
    tet.level = 1 + tet.lines / 10;
    tet.dropInterval = max(120, 600 - (tet.level - 1) * 45);
    playNote(n >= 4 ? 7 : 4);
    oledShowFace(FACE_PIANO_COMBO);
  }
  spawnPiece();
}

// 尝试下移一格, 返回是否成功
static bool softDrop() {
  if (valid(tet.curType, tet.curRot, tet.curX, tet.curY + 1)) {
    tet.curY++;
    return true;
  }
  afterLock();
  return false;
}

static void drawGameOver() {
  lcdClear(COLOR_DARKBG);
  lcdFillRect(0, 90, LCD_WIDTH, 50, COLOR_NAVY);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  lcdDrawString(45, 102, "GAME OVER", 3);
  char buf[24];
  snprintf(buf, sizeof(buf), "Score: %d", tet.score);
  lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
  lcdDrawString(55, 170, buf, 2);
  snprintf(buf, sizeof(buf), "Lines: %d", tet.lines);
  lcdDrawString(55, 200, buf, 1);
  lcdSetTextColor(COLOR_GRAY, COLOR_DARKBG);
  lcdDrawString(45, 240, "ENTER to retry", 2);
  lcdRefresh();
  oledDrawBorderText("Game Over", "ENTER=retry");
  oledShowFace(FACE_PIANO_MISS);
}

void tetrisGameLoop(uint8_t key) {
  if (key == KEY_BACK) {
    currentMode = MODE_MENU;
    drawMenu();
    oledShowWelcome();
    return;
  }

  if (tet.gameOver) {
    if (key == KEY_ENTER) { tetrisGameInit(); return; }
    return;
  }

  // 暂停: 用 5 键(不与移动冲突)
  if (key == KEY_5) {
    tet.paused = !tet.paused;
    if (tet.paused) {
      lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
      lcdDrawString(150, 4, "PAUSE", 1);
      lcdRefresh();
    } else render();
    return;
  }
  if (tet.paused) return;

  bool changed = false;
  // 方向: C/D 或 4/6 左右; A 或 2 旋转; B 或 8 软降; 9 硬降
  if ((key == KEY_LEFT  || key == KEY_4) && valid(tet.curType, tet.curRot, tet.curX - 1, tet.curY)) { tet.curX--; changed = true; }
  if ((key == KEY_RIGHT || key == KEY_6) && valid(tet.curType, tet.curRot, tet.curX + 1, tet.curY)) { tet.curX++; changed = true; }
  if (key == KEY_UP || key == KEY_2) {  // 旋转
    int nr = (tet.curRot + 1) % 4;
    if (valid(tet.curType, nr, tet.curX, tet.curY)) { tet.curRot = nr; changed = true; }
    else if (valid(tet.curType, nr, tet.curX - 1, tet.curY)) { tet.curRot = nr; tet.curX--; changed = true; }
    else if (valid(tet.curType, nr, tet.curX + 1, tet.curY)) { tet.curRot = nr; tet.curX++; changed = true; }
  }
  if (key == KEY_DOWN || key == KEY_8) { softDrop(); changed = true; tet.lastDrop = millis(); }
  if (key == KEY_FIRE) {  // 硬降: 一落到底
    while (valid(tet.curType, tet.curRot, tet.curX, tet.curY + 1)) tet.curY++;
    afterLock();
    changed = true;
    tet.lastDrop = millis();
  }

  // 自动下落
  if (millis() - tet.lastDrop >= tet.dropInterval) {
    tet.lastDrop = millis();
    softDrop();
    changed = true;
  }

  if (changed) {
    if (tet.gameOver) { drawGameOver(); return; }
    render();
  }
}



