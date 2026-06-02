/*
 * plane_game.cpp - 飞机大战游戏 (帧缓冲渲染 + OLED表情)
 */
#include "plane_game.h"
#include "game_common.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "audio.h"
#include "keyboard.h"

#define MAX_BULLETS   8
#define MAX_ENEMIES   6
#define PLAYER_W      16
#define PLAYER_H      16
#define BULLET_SPEED  12
#define ENEMY_SPEED   3
#define PLAYER_SPEED  6

struct Bullet {
  int x, y;
  bool active;
};

struct Enemy {
  int x, y;
  int w, h;
  int hp;
  bool active;
};

static int playerX, playerY;
static int lives;
static int score;
static bool gameOver;
static Bullet bullets[MAX_BULLETS];
static Enemy enemies[MAX_ENEMIES];
static unsigned long lastFrame;
static unsigned long lastEnemySpawn;
static unsigned long enemySpawnInterval;
static int level;
static int frameCount;
static unsigned long lastOledUpdate;

static const uint8_t PROGMEM playerBitmap[] = {
  0x01,0x80, 0x01,0x80, 0x03,0xC0, 0x03,0xC0,
  0x07,0xE0, 0x07,0xE0, 0x0F,0xF0, 0x0F,0xF0,
  0x1F,0xF8, 0x1F,0xF8, 0x3F,0xFC, 0x3F,0xFC,
  0x7F,0xFE, 0x7F,0xFE, 0x3B,0xDC, 0x1F,0xF8
};

void planeGameInit() {
  playerX = LCD_WIDTH / 2 - PLAYER_W / 2;
  playerY = LCD_HEIGHT - PLAYER_H - 2;
  lives = 3;
  score = 0;
  gameOver = false;
  level = 1;
  frameCount = 0;
  enemySpawnInterval = 1500;
  lastFrame = millis();
  lastEnemySpawn = millis();
  lastOledUpdate = 0;

  for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
  for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;

  oledClear();
  oledDrawString(10, 0,  "Plane Battle");
  oledDrawString(10, 18, "Arrows:Move");
  oledDrawString(10, 36, "C:Fire");
  oledRefresh();
}

static void fireBullet() {
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) {
      bullets[i].active = true;
      bullets[i].x = playerX + PLAYER_W / 2;
      bullets[i].y = playerY - 4;
      playSFX_fire();
      return;
    }
  }
}

static void spawnEnemy() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      enemies[i].active = true;
      enemies[i].w = 15 + random(15);
      enemies[i].h = 20;
      enemies[i].x = random(LCD_WIDTH - enemies[i].w);
      enemies[i].y = -enemies[i].h;
      enemies[i].hp = 1 + level / 3;
      return;
    }
  }
}

static void updateBullets() {
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (bullets[i].active) {
      bullets[i].y -= BULLET_SPEED;
      if (bullets[i].y < 0) bullets[i].active = false;
    }
  }
}

static void updateEnemies() {
  int speed = ENEMY_SPEED + level / 2;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) {
      enemies[i].y += speed;
      if (enemies[i].y > LCD_HEIGHT) {
        enemies[i].active = false;
        lives--;
        if (lives <= 0) gameOver = true;
      }
    }
  }
}

static void checkCollisions() {
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;
    for (int j = 0; j < MAX_ENEMIES; j++) {
      if (!enemies[j].active) continue;
      if (bullets[i].x >= enemies[j].x &&
          bullets[i].x <= enemies[j].x + enemies[j].w &&
          bullets[i].y >= enemies[j].y &&
          bullets[i].y <= enemies[j].y + enemies[j].h) {
        bullets[i].active = false;
        enemies[j].hp--;
        if (enemies[j].hp <= 0) {
          enemies[j].active = false;
          score += 10 * level;
          playSFX_explode();
        }
        break;
      }
    }
  }

  for (int j = 0; j < MAX_ENEMIES; j++) {
    if (!enemies[j].active) continue;
    if (enemies[j].x < playerX + PLAYER_W &&
        enemies[j].x + enemies[j].w > playerX &&
        enemies[j].y < playerY + PLAYER_H &&
        enemies[j].y + enemies[j].h > playerY) {
      enemies[j].active = false;
      lives--;
      if (lives <= 0) gameOver = true;
    }
  }
}

// 离散按键事件: 单次移动一步 + 射击。每个按键事件立即处理, 不受帧率限制。
static void handleKeyEvent(uint8_t key) {
  if (key == KEY_LEFT  && playerX > 0)                    playerX -= PLAYER_SPEED;
  if (key == KEY_RIGHT && playerX < LCD_WIDTH - PLAYER_W) playerX += PLAYER_SPEED;
  if (key == KEY_UP    && playerY > 0)                    playerY -= PLAYER_SPEED;
  if (key == KEY_DOWN  && playerY < LCD_HEIGHT - PLAYER_H) playerY += PLAYER_SPEED;
  if (key == KEY_FIRE || key == KEY_ENTER) fireBullet();
}

// 连续移动: 按住左右键时持续平移。放在帧率限制内, 速度才稳定。
static void handleContinuousMove() {
  if (isKeyPressed(KEY_LEFT)  && playerX > 0)                    playerX -= 1;
  if (isKeyPressed(KEY_RIGHT) && playerX < LCD_WIDTH - PLAYER_W) playerX += 1;
}

static void render() {
  lcdClear();

  lcdDrawBitmap(playerX, playerY, PLAYER_W, PLAYER_H, playerBitmap);

  for (int i = 0; i < MAX_BULLETS; i++) {
    if (bullets[i].active) {
      lcdDrawLine(bullets[i].x, bullets[i].y, bullets[i].x, bullets[i].y + 12);
    }
  }

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) {
      lcdFillRect(enemies[i].x, enemies[i].y, enemies[i].w, enemies[i].h);
    }
  }

  char buf[20];
  snprintf(buf, sizeof(buf), "L:%d S:%d Lv:%d", lives, score, level);
  lcdDrawString(0, 0, buf);

  lcdRefresh();
}

static void updateOLED() {
  char top[16], bottom[16];
  snprintf(top, sizeof(top), "Score:%d", score);
  snprintf(bottom, sizeof(bottom), "Lives:%d", lives);
  oledDrawBorderText(top, bottom);
  oledShowBlinkingFace(FACE_IDLE_A, FACE_IDLE_B, 1000);
}

void planeGameLoop(uint8_t key) {
  if (key == KEY_BACK) {
    currentMode = MODE_MENU;
    drawMenu();
    oledShowWelcome();
    return;
  }

  if (gameOver) {
    lcdClear();
    lcdDrawString(80, 30, "GAME OVER");
    char buf[32];
    snprintf(buf, sizeof(buf), "Final Score: %d", score);
    lcdDrawString(60, 80, buf);
    snprintf(buf, sizeof(buf), "Level: %d", level);
    lcdDrawString(80, 120, buf);
    lcdDrawString(60, 180, "ENTER to restart");
    lcdRefresh();

    oledDrawBorderText("Game Over", "ENTER=restart");
    oledShowFace(FACE_PIANO_MISS);

    if (key == KEY_ENTER) planeGameInit();
    return;
  }

  // 按键事件每次都立即处理 (尤其射击), 不能被下面的帧率限制丢弃
  handleKeyEvent(key);

  if (millis() - lastFrame < FRAME_MS) return;
  lastFrame = millis();
  frameCount++;

  handleContinuousMove();
  updateBullets();
  updateEnemies();
  checkCollisions();

  if (millis() - lastEnemySpawn > enemySpawnInterval) {
    spawnEnemy();
    lastEnemySpawn = millis();
  }

  if (frameCount % 600 == 0) {
    level++;
    enemySpawnInterval = max(400UL, enemySpawnInterval - 200);
  }

  render();

  if (millis() - lastOledUpdate > 200) {
    updateOLED();
    lastOledUpdate = millis();
  }
}
