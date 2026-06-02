/*
 * keyboard.cpp - 4x4矩阵键盘扫描实现
 * 行列扫描法: 逐行输出低电平，读取列状态
 */
#include "keyboard.h"

static const uint8_t rowPins[4] = {KB_ROW1, KB_ROW2, KB_ROW3, KB_ROW4};
static const uint8_t colPins[4] = {KB_COL1, KB_COL2, KB_COL3, KB_COL4};

static uint8_t lastKey = KEY_NONE;
static unsigned long lastDebounce = 0;
static const unsigned long DEBOUNCE_MS = 50;

// 键盘映射表 (4行x4列 -> 功能键值)
// 物理布局:
//   1  2  3  A       -> KEY_1, KEY_2, KEY_3, KEY_UP
//   4  5  6  B       -> KEY_4, KEY_5, KEY_6, KEY_DOWN
//   7  8  9  C       -> KEY_7, KEY_8, KEY_FIRE, KEY_LEFT
//   *  0  #  D       -> KEY_BACK, KEY_ENTER, KEY_ENTER, KEY_RIGHT
static const uint8_t keyMap[4][4] = {
  {KEY_1,    KEY_2,     KEY_3,     KEY_UP},
  {KEY_4,    KEY_5,     KEY_6,     KEY_DOWN},
  {KEY_7,    KEY_8,     KEY_FIRE,  KEY_LEFT},
  {KEY_BACK, KEY_ENTER, KEY_ENTER, KEY_RIGHT}
};

void initKeyboard() {
  for (int i = 0; i < 4; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], HIGH);
  }
  for (int i = 0; i < 4; i++) {
    pinMode(colPins[i], INPUT_PULLUP);
  }
}

uint8_t scanKeyboard() {
  if (millis() - lastDebounce < DEBOUNCE_MS) {
    return KEY_NONE;
  }

  for (int row = 0; row < 4; row++) {
    digitalWrite(rowPins[row], LOW);

    for (int col = 0; col < 4; col++) {
      if (digitalRead(colPins[col]) == LOW) {
        delay(10);
        if (digitalRead(colPins[col]) == LOW) {
          digitalWrite(rowPins[row], HIGH);
          uint8_t key = keyMap[row][col];
          if (key != lastKey) {
            lastKey = key;
            lastDebounce = millis();
            return key;
          }
          return KEY_NONE;
        }
      }
    }

    digitalWrite(rowPins[row], HIGH);
  }

  lastKey = KEY_NONE;
  return KEY_NONE;
}

bool isKeyPressed(uint8_t targetKey) {
  for (int row = 0; row < 4; row++) {
    digitalWrite(rowPins[row], LOW);
    for (int col = 0; col < 4; col++) {
      if (digitalRead(colPins[col]) == LOW && keyMap[row][col] == targetKey) {
        digitalWrite(rowPins[row], HIGH);
        return true;
      }
    }
    digitalWrite(rowPins[row], HIGH);
  }
  return false;
}

// ===== 按键事件队列 (环形缓冲, 防漏键; 当前主循环未启用, 备用) =====
static uint8_t keyQueue[KEY_QUEUE_SIZE];
static volatile uint8_t keyQueueHead = 0;
static volatile uint8_t keyQueueTail = 0;

void keyQueuePush(uint8_t key) {
  if (key == KEY_NONE) return;
  uint8_t next = (keyQueueHead + 1) % KEY_QUEUE_SIZE;
  if (next == keyQueueTail) return;  // 满则丢弃
  keyQueue[keyQueueHead] = key;
  keyQueueHead = next;
}

uint8_t keyQueuePop() {
  if (keyQueueHead == keyQueueTail) return KEY_NONE;  // 空
  uint8_t key = keyQueue[keyQueueTail];
  keyQueueTail = (keyQueueTail + 1) % KEY_QUEUE_SIZE;
  return key;
}

bool keyQueueAvailable() { return keyQueueHead != keyQueueTail; }

void keyQueueFlush() { keyQueueHead = keyQueueTail = 0; }
