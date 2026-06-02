/*
 * vocab.cpp - 背单词模块
 * 主屏(LCD)显示英文 (Adafruit GFX 只支持ASCII), 副屏(OLED)显示中文释义 (wqy字体)
 * 两个子模式:
 *   FLASHCARD 翻卡片: 显示英文→按确认看释义→1记得/2不记得→下一个
 *   QUIZ 选择题: 显示英文+4个中文选项, 按1-4选, 报对错计分
 * 入口先选模式: 1=翻卡片 2=选择题; * 返回主菜单
 */
#include "vocab.h"
#include "game_common.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "audio.h"
#include "keyboard.h"
#include <Arduino.h>

// ── 内置词表 (英文, 中文释义) 常用四级/考研词 ──
struct Word { const char* en; const char* zh; };
static const Word WORDS[] = {
  {"abandon",    "放弃; 抛弃"},
  {"ability",    "能力; 才能"},
  {"absorb",     "吸收; 吸引"},
  {"abstract",   "抽象的; 摘要"},
  {"academic",   "学术的"},
  {"accept",     "接受; 承认"},
  {"access",     "接近; 访问"},
  {"accurate",   "精确的"},
  {"achieve",    "实现; 达到"},
  {"acquire",    "获得; 习得"},
  {"adapt",      "适应; 改编"},
  {"adequate",   "充足的"},
  {"adjust",     "调整; 适应"},
  {"advantage",  "优势; 好处"},
  {"affect",     "影响; 感动"},
  {"analyze",    "分析"},
  {"ancient",    "古代的"},
  {"apparent",   "明显的"},
  {"approach",   "接近; 方法"},
  {"appropriate","适当的"},
  {"approve",    "批准; 赞成"},
  {"argue",      "争论; 主张"},
  {"arrange",    "安排; 排列"},
  {"aspect",     "方面; 外观"},
  {"assess",     "评估"},
  {"assume",     "假定; 承担"},
  {"attach",     "附上; 喜爱"},
  {"attitude",   "态度"},
  {"available",  "可用的"},
  {"benefit",    "利益; 受益"},
  {"brief",      "简短的; 简介"},
  {"capable",    "有能力的"},
  {"category",   "类别"},
  {"challenge",  "挑战"},
  {"circumstance","环境; 情况"},
  {"comment",    "评论"},
  {"compare",    "比较"},
  {"complex",    "复杂的"},
  {"concept",    "概念"},
  {"conclude",   "结束; 推断"},
  {"conduct",    "进行; 行为"},
  {"consist",    "组成; 在于"},
  {"constant",   "持续的; 常数"},
  {"contribute", "贡献; 促成"},
  {"convince",   "使信服"},
  {"crucial",    "至关重要的"},
  {"define",     "定义"},
  {"demonstrate","证明; 演示"},
  {"despite",    "尽管"},
  {"distinguish","区分"},
  {"efficient",  "高效的"},
  {"emphasize",  "强调"},
  {"essential",  "必要的; 本质的"},
  {"establish",  "建立"},
  {"evident",    "明显的"},
  {"feature",    "特征; 特色"},
  {"generate",   "产生; 生成"},
  {"identify",   "识别; 确认"},
  {"ignore",     "忽视"},
  {"impact",     "影响; 冲击"}
};
#define WORD_COUNT  (sizeof(WORDS)/sizeof(WORDS[0]))

enum VState { V_PICK, V_CARD_FRONT, V_CARD_BACK, V_QUIZ, V_QUIZ_RESULT, V_DONE };

static struct {
  VState state;
  int    idx;          // 当前词
  int    known, total; // 翻卡: 记得数/已看数
  int    qCorrect, qTotal;  // 测验: 答对/已答
  int    options[4];   // 测验4个选项的词索引
  int    answer;       // 正确选项位置 0-3
  bool   lastRight;
} V;
// PLACEHOLDER_V1
// ── 模式选择画面 ──
static void renderPick() {
  lcdClear(COLOR_DARKBG);
  lcdFillRect(0, 0, LCD_WIDTH, 50, COLOR_NAVY);
  lcdSetTextColor(COLOR_CYAN, COLOR_NAVY);
  lcdDrawString(20, 14, "Vocabulary", 2);

  lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
  lcdDrawString(20, 90,  "1: Flashcard", 2);
  lcdSetTextColor(0x8410, COLOR_DARKBG);
  lcdDrawString(20, 120, "learn word by word", 1);
  lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
  lcdDrawString(20, 160, "2: Quiz", 2);
  lcdSetTextColor(0x8410, COLOR_DARKBG);
  lcdDrawString(20, 190, "choose the meaning", 1);

  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(20, LCD_HEIGHT - 20, "1/2 select   * back", 1);
  lcdRefresh();

  oledClear();
  oledDrawString(8, 8,  "Vocabulary");
  oledDrawString(8, 30, "1 Flashcard");
  oledDrawString(8, 46, "2 Quiz");
  oledRefresh();
}

// ── 翻卡片: 正面(只英文) ──
static void renderCardFront() {
  lcdClear(COLOR_DARKBG);
  lcdFillRect(0, 0, LCD_WIDTH, 40, COLOR_NAVY);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  char prog[24];
  snprintf(prog, sizeof(prog), "Card %d/%d", V.idx + 1, (int)WORD_COUNT);
  lcdDrawString(8, 12, prog, 1);
  lcdSetTextColor(COLOR_CYAN, COLOR_NAVY);
  snprintf(prog, sizeof(prog), "Known:%d", V.known);
  lcdDrawString(150, 12, prog, 1);

  // 大号英文单词
  lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
  lcdDrawString(16, 130, WORDS[V.idx].en, 3);

  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(20, LCD_HEIGHT - 20, "ENTER: show meaning", 1);
  lcdRefresh();

  // 副屏提示
  oledClear();
  oledDrawString(10, 20, "Press ENTER");
  oledDrawString(10, 38, "to see meaning");
  oledRefresh();
}

// ── 翻卡片: 背面(英文+中文释义在OLED) ──
static void renderCardBack() {
  // 主屏保持英文, 下方提示
  lcdFillRect(0, LCD_HEIGHT - 60, LCD_WIDTH, 60, COLOR_DARKBG);
  lcdSetTextColor(COLOR_GREEN, COLOR_DARKBG);
  lcdDrawString(16, 185, "see OLED for meaning", 1);
  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(16, LCD_HEIGHT - 20, "1:Known  2:Again", 1);
  lcdRefresh();

  // 副屏显示中文释义 (wqy字体支持中文)
  oledClear();
  oledDrawString(4, 6,  WORDS[V.idx].en);
  oledDrawString(4, 30, WORDS[V.idx].zh);
  oledRefresh();
}
// PLACEHOLDER_V2
// ── 测验: 为当前词生成4个选项(1个正确+3个干扰) ──
static void setupQuiz() {
  V.options[0] = V.idx;     // 先放正确答案
  // 选3个不同的干扰词
  int n = 1;
  while (n < 4) {
    int cand = random(WORD_COUNT);
    bool dup = false;
    for (int i = 0; i < n; i++) if (V.options[i] == cand) dup = true;
    if (!dup) V.options[n++] = cand;
  }
  // 洗牌
  for (int i = 3; i > 0; i--) {
    int j = random(i + 1);
    int t = V.options[i]; V.options[i] = V.options[j]; V.options[j] = t;
  }
  for (int i = 0; i < 4; i++) if (V.options[i] == V.idx) V.answer = i;
}

// ── 测验画面: 主屏英文+提示, 副屏4个中文选项 ──
static void renderQuiz() {
  lcdClear(COLOR_DARKBG);
  lcdFillRect(0, 0, LCD_WIDTH, 40, COLOR_NAVY);
  lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
  char buf[24];
  snprintf(buf, sizeof(buf), "Q%d  Score:%d/%d", V.qTotal + 1, V.qCorrect, V.qTotal);
  lcdDrawString(8, 12, buf, 1);

  lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
  lcdDrawString(16, 90, WORDS[V.idx].en, 3);
  lcdSetTextColor(0x8410, COLOR_DARKBG);
  lcdDrawString(16, 150, "Pick meaning on OLED", 1);
  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(16, LCD_HEIGHT - 20, "Press 1/2/3/4", 1);
  lcdRefresh();

  // 副屏: 4个中文选项 (中文必须走OLED的wqy字体)
  oledClear();
  char line[40];
  for (int i = 0; i < 4; i++) {
    snprintf(line, sizeof(line), "%d.%s", i + 1, WORDS[V.options[i]].zh);
    oledDrawString(2, i * 16, line);
  }
  oledRefresh();
}

// ── 测验答题反馈 ──
static void renderQuizResult() {
  lcdFillRect(0, 150, LCD_WIDTH, 60, COLOR_DARKBG);
  if (V.lastRight) {
    lcdSetTextColor(COLOR_GREEN, COLOR_DARKBG);
    lcdDrawString(16, 160, "Correct!", 2);
  } else {
    lcdSetTextColor(COLOR_RED, COLOR_DARKBG);
    lcdDrawString(16, 160, "Wrong", 2);
  }
  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(16, LCD_HEIGHT - 20, "ENTER: next", 1);
  // 主屏下方补显正确中文无法用GFX, 故释义放副屏
  lcdRefresh();

  // 副屏显示正确释义
  oledClear();
  oledDrawString(4, 6,  WORDS[V.idx].en);
  oledDrawString(4, 30, WORDS[V.idx].zh);
  oledDrawString(4, 48, V.lastRight ? "OK!" : "X");
  oledRefresh();
}
// PLACEHOLDER_V3
void vocabInit() {
  V.state = V_PICK;
  V.idx = 0;
  V.known = V.total = 0;
  V.qCorrect = V.qTotal = 0;
  randomSeed(micros());
  renderPick();
}

void vocabLoop(uint8_t key) {
  if (key == KEY_BACK) {
    currentMode = MODE_MENU;
    drawMenu();
    oledShowWelcome();
    return;
  }

  switch (V.state) {
    case V_PICK:
      if (key == KEY_1) {                 // 翻卡片
        V.state = V_CARD_FRONT;
        V.idx = random(WORD_COUNT);
        renderCardFront();
      } else if (key == KEY_2) {          // 选择题
        V.state = V_QUIZ;
        V.idx = random(WORD_COUNT);
        setupQuiz();
        renderQuiz();
      }
      break;

    case V_CARD_FRONT:
      if (key == KEY_ENTER) {
        V.state = V_CARD_BACK;
        renderCardBack();
      }
      break;

    case V_CARD_BACK:
      if (key == KEY_1 || key == KEY_2) {
        V.total++;
        if (key == KEY_1) { V.known++; playNote(4); }   // 记得
        else              { playNote(1); }              // 不记得
        // 下一个词(随机, 避免连续重复)
        int prev = V.idx;
        do { V.idx = random(WORD_COUNT); } while (WORD_COUNT > 1 && V.idx == prev);
        V.state = V_CARD_FRONT;
        renderCardFront();
      }
      break;

    case V_QUIZ:
      if (key >= KEY_1 && key <= KEY_4) {
        int pick = key - KEY_1;           // KEY_1..KEY_4 -> 0..3
        V.qTotal++;
        V.lastRight = (pick == V.answer);
        if (V.lastRight) { V.qCorrect++; playNote(7); }
        else             { playSFX_explode(); }
        V.state = V_QUIZ_RESULT;
        renderQuizResult();
      }
      break;

    case V_QUIZ_RESULT:
      if (key == KEY_ENTER) {
        int prev = V.idx;
        do { V.idx = random(WORD_COUNT); } while (WORD_COUNT > 1 && V.idx == prev);
        setupQuiz();
        V.state = V_QUIZ;
        renderQuiz();
      }
      break;

    default: break;
  }
}



