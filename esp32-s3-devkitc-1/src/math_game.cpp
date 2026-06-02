/*
 * math_game.cpp - 数学速算闯关 (选择题形式)
 * 主屏显示算式 + 4个数字选项, 按 1/2/3/4 选; 限时一轮N题, 计分计时
 * 选择题而非直接输入: 键盘物理0=ENTER、9=FIRE, 无法干净输入任意数字, 故用选项
 * 入口先选难度: 1=加减 2=乘除 3=混合; * 返回主菜单
 */
#include "math_game.h"
#include "game_common.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "audio.h"
#include "keyboard.h"
#include <Arduino.h>

#define QUESTIONS_PER_ROUND  10   // 每轮题数

enum MState { M_PICK, M_QUIZ, M_RESULT, M_DONE };

static struct {
  MState state;
  int    diff;          // 0=加减 1=乘除 2=混合
  int    qIndex;        // 当前第几题(0-based)
  int    correct;       // 答对数
  int    answer;        // 本题正确答案
  int    options[4];    // 4个选项
  int    answerPos;     // 正确答案在选项中的位置
  char   expr[24];      // 算式字符串
  bool   lastRight;
  uint32_t roundStart;  // 一轮开始时刻(计总用时)
} M;

// 生成一道题: 难度随题号 qIndex 递增(数值变大/出现三数连算)
static void genQuestion() {
  int a, b, c, op;
  // 难度档: 前1/3 简单, 中段 进阶, 后段 困难
  int tier = M.qIndex * 3 / QUESTIONS_PER_ROUND;   // 0,1,2
  // op: 0+ 1- 2* 3/
  if (M.diff == 0)      op = random(2);          // 加减
  else if (M.diff == 1) op = 2 + random(2);      // 乘除
  else                  op = random(4);          // 混合

  // 三数连算: 进阶档以上, 加减类有概率出现 (如 a+b-c)
  bool triple = (tier >= 1) && (op <= 1) && (random(100) < 40 + tier*20);
  if (triple) {
    int base = (tier >= 2) ? 80 : 40;
    a = random(20, base); b = random(10, base); c = random(1, 30);
    if (random(2)) { M.answer = a + b - c; snprintf(M.expr, sizeof(M.expr), "%d+%d-%d=?", a, b, c); }
    else           { M.answer = a - c + b; snprintf(M.expr, sizeof(M.expr), "%d-%d+%d=?", a, c, b); }
  } else {
    switch (op) {
      case 0: { // 加: 数值随档位变大
        int hi = (tier == 0) ? 50 : (tier == 1) ? 200 : 500;
        a = random(10, hi); b = random(10, hi);
        M.answer = a + b;
        snprintf(M.expr, sizeof(M.expr), "%d + %d = ?", a, b);
        break; }
      case 1: { // 减(非负)
        int hi = (tier == 0) ? 60 : (tier == 1) ? 200 : 600;
        a = random(20, hi); b = random(1, a);
        M.answer = a - b;
        snprintf(M.expr, sizeof(M.expr), "%d - %d = ?", a, b);
        break; }
      case 2: { // 乘: 档位越高乘数越大
        int hi = (tier == 0) ? 10 : (tier == 1) ? 16 : 25;
        a = random(2, hi); b = random(2, hi);
        M.answer = a * b;
        snprintf(M.expr, sizeof(M.expr), "%d x %d = ?", a, b);
        break; }
      default: { // 除(整除), 档位越高被除数范围越大
        int qhi = (tier == 0) ? 10 : (tier == 1) ? 13 : 20;
        b = random(2, 12); { int q = random(2, qhi); a = b * q; M.answer = q; }
        snprintf(M.expr, sizeof(M.expr), "%d / %d = ?", a, b);
        break; }
    }
  }

  // 生成4个选项(1正确 + 3个相近干扰). 干扰幅度随答案大小调整
  M.options[0] = M.answer;
  int spread = M.answer > 50 ? (M.answer / 10 + 2) : 9;
  int n = 1;
  while (n < 4) {
    int delta = random(1, spread + 1) * (random(2) ? 1 : -1);
    int cand = M.answer + delta;
    if (cand < 0) cand = M.answer + abs(delta) + 1;
    bool dup = false;
    for (int i = 0; i < n; i++) if (M.options[i] == cand) dup = true;
    if (!dup) M.options[n++] = cand;
  }
  // 洗牌
  for (int i = 3; i > 0; i--) {
    int j = random(i + 1);
    int t = M.options[i]; M.options[i] = M.options[j]; M.options[j] = t;
  }
  for (int i = 0; i < 4; i++) if (M.options[i] == M.answer) M.answerPos = i;
}
// PLACEHOLDER_M1
static void renderPick() {
  lcdClear(COLOR_DARKBG);
  lcdFillRect(0, 0, LCD_WIDTH, 50, COLOR_NAVY);
  lcdSetTextColor(COLOR_CYAN, COLOR_NAVY);
  lcdDrawString(20, 14, "Math Drill", 2);

  const char* names[3] = {"1: Add / Sub", "2: Mul / Div", "3: Mixed"};
  const uint16_t cols[3] = {COLOR_GREEN, COLOR_GOLD, COLOR_RED};
  for (int i = 0; i < 3; i++) {
    int y = 95 + i * 50;
    lcdFillRect(30, y, 8, 34, cols[i]);
    lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
    lcdDrawString(50, y + 7, names[i], 2);
  }
  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(20, LCD_HEIGHT - 20, "1/2/3 select   * back", 1);
  lcdRefresh();

  oledClear();
  oledDrawString(8, 8,  "Math Drill");
  oledDrawString(8, 30, "Pick 1/2/3");
  oledRefresh();
}

static void renderQuiz() {
  lcdClear(COLOR_DARKBG);
  // 顶部: 进度 + 得分
  lcdFillRect(0, 0, LCD_WIDTH, 40, COLOR_NAVY);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  char buf[24];
  snprintf(buf, sizeof(buf), "Q %d/%d", M.qIndex + 1, QUESTIONS_PER_ROUND);
  lcdDrawString(8, 12, buf, 1);
  snprintf(buf, sizeof(buf), "Correct:%d", M.correct);
  lcdSetTextColor(COLOR_CYAN, COLOR_NAVY);
  lcdDrawString(140, 12, buf, 1);

  // 算式(大号)
  lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
  lcdDrawString(20, 80, M.expr, 3);

  // 4个选项, 2x2 排布, 标号 1-4
  const uint16_t cols[4] = {0x07FF, 0x07E0, 0xFD20, 0xFFE0};
  for (int i = 0; i < 4; i++) {
    int col = i % 2, row = i / 2;
    int x = 20 + col * 110;
    int y = 150 + row * 60;
    lcdFillRect(x, y, 100, 48, 0x1082);
    lcdFillRect(x, y, 6, 48, cols[i]);
    lcdSetTextColor(COLOR_WHITE, 0x1082);
    char ob[16];
    snprintf(ob, sizeof(ob), "%d.%d", i + 1, M.options[i]);
    lcdDrawString(x + 14, y + 14, ob, 2);
  }
  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(40, LCD_HEIGHT - 16, "Press 1/2/3/4", 1);
  lcdRefresh();

  oledDrawBorderText("Math Drill", M.expr);
  oledShowBlinkingFace(FACE_STUDY_A, FACE_STUDY_B, 1000);
}

static void renderResult() {
  lcdFillRect(0, 270, LCD_WIDTH, 40, COLOR_DARKBG);
  lcdSetTextColor(M.lastRight ? COLOR_GREEN : COLOR_RED, COLOR_DARKBG);
  lcdDrawString(20, 278, M.lastRight ? "Correct!" : "Wrong", 2);
  lcdRefresh();
  oledShowFace(M.lastRight ? FACE_PIANO_COMBO : FACE_PIANO_MISS);
}

static void renderDone() {
  lcdClear(COLOR_DARKBG);
  lcdFillRect(0, 0, LCD_WIDTH, 55, COLOR_NAVY);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  lcdDrawString(30, 16, "Round Done!", 2);

  char buf[32];
  int secs = (millis() - M.roundStart) / 1000;
  lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
  snprintf(buf, sizeof(buf), "Score: %d/%d", M.correct, QUESTIONS_PER_ROUND);
  lcdDrawString(40, 110, buf, 2);
  snprintf(buf, sizeof(buf), "Time: %d s", secs);
  lcdDrawString(40, 150, buf, 2);

  // 评级
  const char* grade = (M.correct >= 9) ? "Excellent!" :
                      (M.correct >= 7) ? "Good" :
                      (M.correct >= 5) ? "OK" : "Keep trying";
  lcdSetTextColor(COLOR_GOLD, COLOR_DARKBG);
  lcdDrawString(40, 195, grade, 2);

  lcdSetTextColor(COLOR_GRAY, COLOR_DARKBG);
  lcdDrawString(35, 250, "ENTER: again  *: back", 1);
  lcdRefresh();

  oledDrawBorderText("Done", buf);
  oledShowFace(M.correct >= 7 ? FACE_PIANO_COMBO : FACE_IDLE_A);
}
// PLACEHOLDER_M2
void mathGameInit() {
  M.state = M_PICK;
  M.diff = 0;
  M.qIndex = 0;
  M.correct = 0;
  randomSeed(micros());
  renderPick();
}

static void startRound(int diff) {
  M.diff = diff;
  M.qIndex = 0;
  M.correct = 0;
  M.roundStart = millis();
  genQuestion();
  M.state = M_QUIZ;
  renderQuiz();
}

void mathGameLoop(uint8_t key) {
  if (key == KEY_BACK) {
    currentMode = MODE_MENU;
    drawMenu();
    oledShowWelcome();
    return;
  }

  switch (M.state) {
    case M_PICK:
      if (key == KEY_1) startRound(0);
      else if (key == KEY_2) startRound(1);
      else if (key == KEY_3) startRound(2);
      break;

    case M_QUIZ:
      if (key >= KEY_1 && key <= KEY_4) {
        int pick = key - KEY_1;             // 0..3
        M.lastRight = (pick == M.answerPos);
        if (M.lastRight) { M.correct++; playNote(7); }
        else             { playSFX_explode(); }
        M.state = M_RESULT;
        renderResult();
      }
      break;

    case M_RESULT:
      if (key == KEY_ENTER) {
        M.qIndex++;
        if (M.qIndex >= QUESTIONS_PER_ROUND) {
          M.state = M_DONE;
          playSFX_success();
          renderDone();
        } else {
          genQuestion();
          M.state = M_QUIZ;
          renderQuiz();
        }
      }
      break;

    case M_DONE:
      if (key == KEY_ENTER) { M.state = M_PICK; renderPick(); }
      break;
  }
}


