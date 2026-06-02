/*
 * keypad_test.cpp - 4x4键盘自检 (独立测试, 不影响游戏主程序)
 * 按下任意键, 串口打印它的"行/列"以及当前keyMap翻译出的功能。
 * 用途: 换新键盘后, 挨个按16个键, 据此把 keyboard.cpp 的 keyMap 改对。
 *
 * 编译上传: platformio.exe run -e keypad_test --target upload --upload-port COM6
 * 看输出:   platformio.exe device monitor -e keypad_test
 */
#include <Arduino.h>
#include "config.h"

static const uint8_t rowPins[4] = {KB_ROW1, KB_ROW2, KB_ROW3, KB_ROW4};
static const uint8_t colPins[4] = {KB_COL1, KB_COL2, KB_COL3, KB_COL4};

// 当前游戏里的 keyMap (与 keyboard.cpp 保持一致), 仅用于对照显示
static const char* keyName[4][4] = {
  {"KEY_1",    "KEY_2",     "KEY_3",     "KEY_UP"},
  {"KEY_4",    "KEY_5",     "KEY_6",     "KEY_DOWN"},
  {"KEY_7",    "KEY_8",     "KEY_FIRE",  "KEY_LEFT"},
  {"KEY_BACK", "KEY_ENTER", "KEY_ENTER", "KEY_RIGHT"}
};

static int lastRow = -1, lastCol = -1;

void setup() {
  Serial.begin(115200);
  delay(1800);
  for (int i = 0; i < 4; i++) { pinMode(rowPins[i], OUTPUT); digitalWrite(rowPins[i], HIGH); }
  for (int i = 0; i < 4; i++) { pinMode(colPins[i], INPUT_PULLUP); }

  Serial.println();
  Serial.println("==================================");
  Serial.println("   4x4 键盘自检");
  Serial.println("==================================");
  Serial.println("引脚: 行 ROW=1/2/3/5, 列 COL=14/15/16/17");
  Serial.println("请挨个按键, 看每个键报告的 行/列, 把结果告诉我。");
  Serial.println();
}

void loop() {
  int foundRow = -1, foundCol = -1;
  for (int row = 0; row < 4; row++) {
    digitalWrite(rowPins[row], LOW);
    for (int col = 0; col < 4; col++) {
      if (digitalRead(colPins[col]) == LOW) { foundRow = row; foundCol = col; }
    }
    digitalWrite(rowPins[row], HIGH);
  }

  // 只在按下状态变化时打印一次, 避免刷屏
  if (foundRow != -1 && (foundRow != lastRow || foundCol != lastCol)) {
    Serial.printf("按下: 第%d行 第%d列  (row=%d,col=%d)  -> 当前映射: %s\n",
                  foundRow + 1, foundCol + 1, foundRow, foundCol, keyName[foundRow][foundCol]);
  }
  if (foundRow == -1 && lastRow != -1) {
    // 松开
  }
  lastRow = foundRow;
  lastCol = foundCol;
  delay(30);
}
