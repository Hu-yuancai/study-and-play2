/*
 * study_buddy.cpp - 学习伴侣实现
 * 番茄工作法计时器 + OLED表情动画
 */
#include "study_buddy.h"
#include "game_common.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "audio.h"
#include "keyboard.h"

enum StudyState {
  STUDY_SELECT,
  STUDY_RUNNING,
  STUDY_PAUSED,
  STUDY_DONE
};

static StudyState state;
static int timerModes[] = {25, 30, 45, 60};
static int modeCount = 4;
static int selectedMode;
static unsigned long totalSeconds;
static unsigned long remainSeconds;
static unsigned long lastTick;
static unsigned long startTime;
static int encourageCount;
static unsigned long lastOledBlink;

static void renderSelect();
static void renderTimer();
static void renderDone();

void studyBuddyInit() {
  state = STUDY_SELECT;
  selectedMode = 0;
  encourageCount = 0;
  lastOledBlink = millis();
  renderSelect();
  oledShowBlinkingFace(FACE_STUDY_A, FACE_STUDY_B, 800);
}

static const uint16_t modeColors[4] = {COLOR_CYAN, COLOR_GREEN, COLOR_GOLD, COLOR_ORANGE};

static void renderSelect() {
  lcdClear(COLOR_DARKBG);
  // 标题栏 (新配色: 浅薄荷绿底 + 深绿字)
  lcdFillRect(0, 0, LCD_WIDTH, 45, 0xB7F7);
  lcdSetTextColor(0x24A4, 0xB7F7);
  lcdDrawString(30, 12, "Study Buddy", 2);

  // 四个时长选项
  for (int i = 0; i < modeCount; i++) {
    bool sel = (i == selectedMode);
    int y = 95 + i * 55;
    if (sel) {
      lcdDrawRect(25, y - 2, LCD_WIDTH - 50, 46, modeColors[i]);
      lcdDrawRect(26, y - 1, LCD_WIDTH - 52, 44, modeColors[i]);
      lcdSetTextColor(modeColors[i], COLOR_DARKBG);
    } else {
      lcdSetTextColor(0x632C, COLOR_DARKBG);
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%d min", timerModes[i]);
    lcdDrawString(55, y + 4, buf, 2);
  }

  // 底部提示
  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(45, LCD_HEIGHT - 20, "UP/DOWN  ENTER", 1);
  lcdRefresh();
}

static void renderTimer() {
  lcdClear(COLOR_DARKBG);
  // 标题栏 (新配色: 浅薄荷绿底 + 深绿字)
  lcdFillRect(0, 0, LCD_WIDTH, 45, 0xB7F7);
  lcdSetTextColor(0x24A4, 0xB7F7);
  // 已过时间百分比
  int pct = (int)(100 - ((remainSeconds * 100) / totalSeconds));
  char titleBuf[24];
  snprintf(titleBuf, sizeof(titleBuf), "Focusing  %d%%", pct);
  lcdDrawString(25, 8, titleBuf, 2);

  // 大时间
  int mins = remainSeconds / 60;
  int secs = remainSeconds % 60;
  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mins, secs);
  lcdSetTextColor(0xADEF, COLOR_DARKBG);   // 倒计时数字: 淡黄绿
  lcdDrawString(30, 100, timeBuf, 4);

  // 进度条
  int barY = 180;
  int barW = LCD_WIDTH - 40;
  int progress = ((totalSeconds - remainSeconds) * barW) / totalSeconds;
  lcdDrawRect(20, barY, barW, 16, 0x4208);
  if (progress > 2) {
    uint16_t barColor = (pct < 50) ? COLOR_CYAN : (pct < 85) ? COLOR_GREEN : COLOR_GOLD;
    lcdFillRect(22, barY + 2, progress - 4, 12, barColor);
  }

  // 底部
  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(55, 240, "ENTER to pause", 1);
  lcdRefresh();
}

static void renderDone() {
  lcdClear(COLOR_DARKBG);
  // 标题 (新配色: 绿底金字, 放大)
  lcdFillRect(0, 0, LCD_WIDTH, 55, COLOR_GREEN);
  lcdSetTextColor(COLOR_GOLD, COLOR_GREEN);
  lcdDrawString(20, 10, "Congratulations!", 2);

  // 完成信息
  lcdSetTextColor(0xADEF, COLOR_DARKBG);
  char buf[32];
  snprintf(buf, sizeof(buf), "Session: %d min", (int)(totalSeconds / 60));
  lcdDrawString(40, 90, buf, 2);
  snprintf(buf, sizeof(buf), "Encouraged: %d times", encourageCount);
  lcdDrawString(30, 130, buf, 1);

  // 装饰线
  lcdDrawLine(30, 180, LCD_WIDTH - 30, 180, 0x4208);

  lcdSetTextColor(COLOR_CYAN, COLOR_DARKBG);
  lcdDrawString(45, 220, "ENTER to continue", 2);
  lcdRefresh();
}

void studyBuddyLoop(uint8_t key) {
  if (key == KEY_BACK) {
    stopTone();
    currentMode = MODE_MENU;
    drawMenu();
    oledShowWelcome();
    return;
  }

  switch (state) {
    case STUDY_SELECT:
      if (key == KEY_UP) {
        selectedMode = (selectedMode + modeCount - 1) % modeCount;
        renderSelect();
      } else if (key == KEY_DOWN) {
        selectedMode = (selectedMode + 1) % modeCount;
        renderSelect();
      } else if (key == KEY_ENTER) {
        totalSeconds = timerModes[selectedMode] * 60UL;
        remainSeconds = totalSeconds;
        lastTick = millis();
        startTime = millis();
        state = STUDY_RUNNING;
        oledShowBlinkingFace(FACE_STUDY_A, FACE_STUDY_B, 800);
        renderTimer();
      }
      break;

    case STUDY_RUNNING:
      if (key == KEY_ENTER) {
        state = STUDY_PAUSED;
        oledShowBlinkingFace(FACE_PAUSE_A, FACE_PAUSE_B, 600);
        lcdDrawString(100, 100, "[PAUSED]");
        lcdRefresh();
        return;
      }

      // 专注期间副屏循环播放动态表情包陪伴
      if (millis() - lastOledBlink > 500) {
        oledPlayEmojiIdle(600, 4500);
        lastOledBlink = millis();
      }

      if (millis() - lastTick >= 1000) {
        lastTick += 1000;
        if (remainSeconds > 0) {
          remainSeconds--;
          renderTimer();

          if (remainSeconds > 0 && remainSeconds % 300 == 0) {
            encourageCount++;
            oledShowBlinkingFace(FACE_STUDY_A, FACE_STUDY_B, 400);
          }
        }

        if (remainSeconds == 0) {
          state = STUDY_DONE;
          renderDone();
          oledShowBlinkingFace(FACE_DONE_A, FACE_DONE_B, 500);
          playSFX_success();
        }
      }
      break;

    case STUDY_PAUSED:
      if (key == KEY_ENTER) {
        state = STUDY_RUNNING;
        lastTick = millis();
        renderTimer();
        oledShowBlinkingFace(FACE_STUDY_A, FACE_STUDY_B, 800);
      }
      break;

    case STUDY_DONE:
      if (key == KEY_ENTER) {
        studyBuddyInit();
      }
      break;
  }
}
