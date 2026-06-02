/*
 * snake_game.cpp - 贪吃蛇游戏
 * 网格 15x16, 方向键转向(不能180度掉头), 定时前进, 吃果子加长加分加速
 * A/上 B/下 C/左 D/右 控制方向; 0/# 暂停; * 返回; 结束后确认重开
 */
#include "snake_game.h"
#include "game_common.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "audio.h"
#include "keyboard.h"
#include <Arduino.h>

#define CELL      16
#define HUD_H     40                       // 顶部计分栏高度
#define COLS      (LCD_WIDTH / CELL)        // 240/16 = 15
#define ROWS      ((LCD_HEIGHT - HUD_H) / CELL)  // 280/16 = 17
#define MAX_LEN   (COLS * ROWS)

// 方向: 0上 1下 2左 3右
static int dirX[4] = {0, 0, -1, 1};
static int dirY[4] = {-1, 1, 0, 0};

static struct {
  int   bodyX[MAX_LEN];
  int   bodyY[MAX_LEN];
  int   length;
  int   dir;          // 当前方向
  int   pendingDir;   // 缓存的下一方向(防同帧多次转向)
  int   foodX, foodY;
  int   score;
  bool  gameOver;
  bool  paused;
  uint32_t lastStep;
  uint32_t stepInterval;  // 前进间隔(ms), 越小越快
} snk;

// 前置声明
static void render();

// 把果子随机放到一个不在蛇身上的格子
static void placeFood() {
  bool occupied;
  do {
    occupied = false;
    snk.foodX = random(COLS);
    snk.foodY = random(ROWS);
    for (int i = 0; i < snk.length; i++) {
      if (snk.bodyX[i] == snk.foodX && snk.bodyY[i] == snk.foodY) {
        occupied = true; break;
      }
    }
  } while (occupied);
}

// 把格子坐标转成屏幕像素并画一个方块
static void drawCell(int gx, int gy, uint16_t color) {
  int px = gx * CELL;
  int py = HUD_H + gy * CELL;
  lcdFillRect(px + 1, py + 1, CELL - 2, CELL - 2, color);
}
// PLACEHOLDER_REST
void snakeGameInit() {
  snk.length = 3;
  // 蛇初始在中间, 朝右
  int cx = COLS / 2, cy = ROWS / 2;
  for (int i = 0; i < snk.length; i++) {
    snk.bodyX[i] = cx - i;
    snk.bodyY[i] = cy;
  }
  snk.dir = 3;          // 右
  snk.pendingDir = 3;
  snk.score = 0;
  snk.gameOver = false;
  snk.paused = false;
  snk.stepInterval = 220;
  snk.lastStep = millis();
  randomSeed(micros());
  placeFood();

  oledDrawBorderText("Snake", "Arrows move");
  oledShowBlinkingFace(FACE_IDLE_A, FACE_IDLE_B, 800);
  render();
}

static void drawHUD() {
  lcdFillRect(0, 0, LCD_WIDTH, HUD_H, COLOR_NAVY);
  lcdSetTextColor(COLOR_CYAN, COLOR_NAVY);
  lcdDrawString(8, 12, "SNAKE", 2);
  char buf[20];
  snprintf(buf, sizeof(buf), "Score:%d", snk.score);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  lcdDrawString(120, 14, buf, 1);
}

static void render() {
  lcdClear(COLOR_BLACK);
  drawHUD();
  // 果子(红)
  drawCell(snk.foodX, snk.foodY, COLOR_RED);
  // 蛇身(头亮绿, 身体暗绿)
  for (int i = 0; i < snk.length; i++) {
    drawCell(snk.bodyX[i], snk.bodyY[i], i == 0 ? COLOR_GREEN : 0x05E0);
  }
  lcdRefresh();
}
// PLACEHOLDER_LOOP
// 前进一步: 计算新蛇头, 判定撞墙/撞身/吃果
static void step() {
  snk.dir = snk.pendingDir;
  int nx = snk.bodyX[0] + dirX[snk.dir];
  int ny = snk.bodyY[0] + dirY[snk.dir];

  // 撞墙
  if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
    snk.gameOver = true;
    playSFX_explode();
    return;
  }
  // 撞自己(不含尾巴, 因为尾巴会让出位置; 简化为检查除尾外的身体)
  for (int i = 0; i < snk.length - 1; i++) {
    if (snk.bodyX[i] == nx && snk.bodyY[i] == ny) {
      snk.gameOver = true;
      playSFX_explode();
      return;
    }
  }

  bool ate = (nx == snk.foodX && ny == snk.foodY);
  // 身体后移
  int newLen = ate ? snk.length + 1 : snk.length;
  if (newLen > MAX_LEN) newLen = MAX_LEN;
  for (int i = newLen - 1; i > 0; i--) {
    snk.bodyX[i] = snk.bodyX[i - 1];
    snk.bodyY[i] = snk.bodyY[i - 1];
  }
  snk.bodyX[0] = nx;
  snk.bodyY[0] = ny;
  snk.length = newLen;

  if (ate) {
    snk.score += 10;
    playNote(4);  // 吃果子提示音
    placeFood();
    // 每吃几个加速一点
    if (snk.stepInterval > 90) snk.stepInterval -= 6;
    oledShowFace(FACE_PIANO_COMBO);
  }
}

static void drawGameOver() {
  lcdClear(COLOR_DARKBG);
  lcdFillRect(0, 80, LCD_WIDTH, 50, COLOR_NAVY);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  lcdDrawString(45, 92, "GAME OVER", 3);
  char buf[24];
  snprintf(buf, sizeof(buf), "Score: %d", snk.score);
  lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
  lcdDrawString(60, 160, buf, 2);
  lcdSetTextColor(COLOR_GRAY, COLOR_DARKBG);
  lcdDrawString(45, 220, "ENTER to retry", 2);
  lcdRefresh();
  oledDrawBorderText("Game Over", "ENTER=retry");
  oledShowFace(FACE_PIANO_MISS);
}

void snakeGameLoop(uint8_t key) {
  if (key == KEY_BACK) {
    currentMode = MODE_MENU;
    drawMenu();
    oledShowWelcome();
    return;
  }

  if (snk.gameOver) {
    if (key == KEY_ENTER) { snakeGameInit(); render(); return; }
    return;
  }

  // 转向(不能直接180度掉头). 方向键: A/B/C/D 或 数字 2/8/4/6
  if ((key == KEY_UP    || key == KEY_2) && snk.dir != 1) snk.pendingDir = 0;
  if ((key == KEY_DOWN  || key == KEY_8) && snk.dir != 0) snk.pendingDir = 1;
  if ((key == KEY_LEFT  || key == KEY_4) && snk.dir != 3) snk.pendingDir = 2;
  if ((key == KEY_RIGHT || key == KEY_6) && snk.dir != 2) snk.pendingDir = 3;
  if (key == KEY_ENTER) {           // 暂停/继续
    snk.paused = !snk.paused;
    if (snk.paused) {
      lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
      lcdDrawString(80, 12, "PAUSE", 2);
      lcdRefresh();
    }
  }
  if (snk.paused) return;

  // 定时前进
  if (millis() - snk.lastStep >= snk.stepInterval) {
    snk.lastStep = millis();
    step();
    if (snk.gameOver) { drawGameOver(); return; }
    render();
  }
}


