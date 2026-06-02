/*
 * piano_game.cpp - 钢琴块游戏 (6曲目, 长按支持, OLED表情, 帧缓冲渲染)
 */
#include "piano_game.h"
#include "game_common.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "audio.h"
#include "keyboard.h"
#include "config.h"
#include <Arduino.h>

// ========== 游戏参数 ==========
#define NOTE_LANES      4
#define LANE_WIDTH      (LCD_WIDTH / NOTE_LANES)
#define NOTE_HEIGHT     26
#define JUDGE_LINE_Y    280
#define MAX_NOTES_ON_SCREEN 30
#define NOTE_POOL_SIZE  300

// 判定窗口与下落速度改为运行时变量, 由难度决定 (见 setDifficulty)
// PERFECT/GOOD/BAD: 判定容差(ms, 越大越易); APPROACH_MS: 音符从顶部落到判定线耗时(越大越慢越易)
static int   PERFECT_MS = 120;
static int   GOOD_MS    = 200;
static int   BAD_MS     = 280;
static float APPROACH_MS = 2200.0f;
#define PX_PER_MS    ((float)JUDGE_LINE_Y / APPROACH_MS)

// 难度: 0=简单 1=普通 2=困难
static int pianoDifficulty = 1;
static const char* DIFF_NAMES[3] = {"Easy", "Normal", "Hard"};

static void setDifficulty(int d) {
  pianoDifficulty = d;
  switch (d) {
    case 0: PERFECT_MS=150; GOOD_MS=240; BAD_MS=340; APPROACH_MS=2800.0f; break; // 慢+宽松
    case 1: PERFECT_MS=120; GOOD_MS=200; BAD_MS=280; APPROACH_MS=2200.0f; break; // 普通
    case 2: PERFECT_MS=70;  GOOD_MS=120; BAD_MS=160; APPROACH_MS=1400.0f; break; // 快+严格
  }
}


#define MAX_SONGS 6

struct RuntimeNote {
  uint32_t tick;
  uint8_t  lane;      // 显示轨道(由音高自动映射)
  uint8_t  pitch;     // 真实音高索引 (0=C4 .. 12=A5)
  uint8_t  type;
  uint32_t duration;  // 发声/方块时值(ms)
  bool     active;
  bool     sounded;   // 是否已自动发声(避免重复)
};

struct NoteProto {
  uint32_t tick;      // 起始时刻(ms)
  uint8_t  pitch;     // 真实音高索引 (0=C4 1=D4 2=E4 3=F4 4=G4 5=A4 6=B4 7=C5 8=D5 9=E5 10=F5 11=G5 12=A5)
  uint8_t  type;      // 0=点击 1=长按
  uint32_t duration;  // 该音持续时长(ms): 四分≈360, 二分≈760, 全音符≈1160
};

// ========== 6首曲目 ==========
// 音高索引: 0=C4(do) 1=D4(re) 2=E4(mi) 3=F4(fa) 4=G4(sol) 5=A4(la) 6=B4(si)
//          7=C5(do') 8=D5 9=E5 10=F5 11=G5 12=A5
static const char* const songNames[MAX_SONGS] PROGMEM = {
  "Twinkle Star",   // 小星星
  "Happy Birthday", // 生日快乐
  "Little Star v2", // 一闪一闪(B段)
  "Ode to Joy",     // 欢乐颂
  "Jingle Bells",   // 铃儿响叮当
  "Mary's Lamb"     // 玛丽有只小羊羔
};

// 节拍常量: BEAT=一拍间隔(ms), 音符时值略短于间隔以分离音
#define BEAT  450
#define Q     (BEAT)          // 四分音符
#define H     (BEAT*2)        // 二分音符
#define QD    380             // 四分音符发声时长
#define HD    780             // 二分音符发声时长

// 歌曲1: 小星星  1 1 5 5 6 6 5 - | 4 4 3 3 2 2 1 -
static const NoteProto PROGMEM song0[] = {
  {0*Q,0,0,QD},{1*Q,0,0,QD},{2*Q,4,0,QD},{3*Q,4,0,QD},
  {4*Q,5,0,QD},{5*Q,5,0,QD},{6*Q,4,0,HD},
  {8*Q,3,0,QD},{9*Q,3,0,QD},{10*Q,2,0,QD},{11*Q,2,0,QD},
  {12*Q,1,0,QD},{13*Q,1,0,QD},{14*Q,0,0,HD},
  {0xFFFFFFFF,0,0,0}
};

// 歌曲2: 生日快乐  5 5 6 5 1' 7 - | 5 5 6 5 2' 1' -
static const NoteProto PROGMEM song1[] = {
  {0*Q,4,0,QD},{1*Q,4,0,QD},{2*Q,5,0,QD},{3*Q,4,0,QD},
  {4*Q,7,0,QD},{5*Q,6,0,HD},
  {7*Q,4,0,QD},{8*Q,4,0,QD},{9*Q,5,0,QD},{10*Q,4,0,QD},
  {11*Q,8,0,QD},{12*Q,7,0,HD},
  {0xFFFFFFFF,0,0,0}
};

// 歌曲3: 一闪一闪亮晶晶(小星星B段+尾)  5 5 4 4 3 3 2 - (x2) 再回主题
static const NoteProto PROGMEM song2[] = {
  {0*Q,4,0,QD},{1*Q,4,0,QD},{2*Q,3,0,QD},{3*Q,3,0,QD},
  {4*Q,2,0,QD},{5*Q,2,0,QD},{6*Q,1,0,HD},
  {8*Q,4,0,QD},{9*Q,4,0,QD},{10*Q,3,0,QD},{11*Q,3,0,QD},
  {12*Q,2,0,QD},{13*Q,2,0,QD},{14*Q,1,1,HD},
  {0xFFFFFFFF,0,0,0}
};

// 歌曲4: 欢乐颂  3 3 4 5 5 4 3 2 | 1 1 2 3 3. 2 2
static const NoteProto PROGMEM song3[] = {
  {0*Q,2,0,QD},{1*Q,2,0,QD},{2*Q,3,0,QD},{3*Q,4,0,QD},
  {4*Q,4,0,QD},{5*Q,3,0,QD},{6*Q,2,0,QD},{7*Q,1,0,QD},
  {8*Q,0,0,QD},{9*Q,0,0,QD},{10*Q,1,0,QD},{11*Q,2,0,QD},
  {12*Q,2,0,580},{12*Q+300,1,0,200},{13*Q+300,1,0,HD},
  {0xFFFFFFFF,0,0,0}
};

// 歌曲5: 铃儿响叮当  3 3 3 - | 3 3 3 - | 3 5 1 2 3 -
static const NoteProto PROGMEM song4[] = {
  {0*Q,2,0,QD},{1*Q,2,0,QD},{2*Q,2,0,HD},
  {4*Q,2,0,QD},{5*Q,2,0,QD},{6*Q,2,0,HD},
  {8*Q,2,0,QD},{9*Q,4,0,QD},{10*Q,0,0,QD},{11*Q,1,0,QD},
  {12*Q,2,0,HD},
  {0xFFFFFFFF,0,0,0}
};

// 歌曲6: 玛丽有只小羊羔  3 2 1 2 3 3 3 - | 2 2 2 - 3 5 5 -
static const NoteProto PROGMEM song5[] = {
  {0*Q,2,0,QD},{1*Q,1,0,QD},{2*Q,0,0,QD},{3*Q,1,0,QD},
  {4*Q,2,0,QD},{5*Q,2,0,QD},{6*Q,2,0,HD},
  {8*Q,1,0,QD},{9*Q,1,0,QD},{10*Q,1,0,HD},
  {12*Q,2,0,QD},{13*Q,4,0,QD},{14*Q,4,0,HD},
  {0xFFFFFFFF,0,0,0}
};

static const NoteProto* const songData[MAX_SONGS] PROGMEM = {
  song0, song1, song2, song3, song4, song5
};
static const int songLengths[MAX_SONGS] = {
  sizeof(song0)/sizeof(song0[0]),
  sizeof(song1)/sizeof(song1[0]),
  sizeof(song2)/sizeof(song2[0]),
  sizeof(song3)/sizeof(song3[0]),
  sizeof(song4)/sizeof(song4[0]),
  sizeof(song5)/sizeof(song5[0])
};

// ========== 全局游戏状态 ==========
static struct {
  int currentSong;
  RuntimeNote pool[NOTE_POOL_SIZE];
  int noteCount;
  int nextLoadIdx;

  struct ActiveNote {
    int poolIdx;
    int x, y;
    bool holdActive;
    int  holdJudge;
    uint32_t pressTime;
  };
  ActiveNote active[MAX_NOTES_ON_SCREEN];
  int activeCnt;

  int perfect, good, bad, miss;
  int combo, maxCombo;
  float acc;
  bool gameOver;
  bool paused;
  bool selectingDiff;   // 正在选难度(进入游戏先选)

  uint32_t gameStartTime;
  uint32_t audioStartTime;

  bool keyState[4];
  uint32_t holdStartTime[4];

  int lastHitType;
  uint32_t lastHitTime;
  uint32_t lastOledUpdate;
  int lastKeyLane;
  uint32_t lastKeyTime;

  char currentSongName[32];
} game;

// ========== 辅助函数 ==========
static void computeACC() {
  int total = game.perfect + game.good + game.bad + game.miss;
  game.acc = (total == 0) ? 100.0f
            : (game.perfect + game.good * 0.5f) / total * 100.0f;
}

static void setHitFeedback(int type) {
  game.lastHitType = type;
  game.lastHitTime = millis();
}

// 音高(0..12)映射到4条显示轨道: 低→高 从左到右, 旋律起伏带动方块左右错落
static uint8_t pitchToLane(uint8_t pitch) {
  if (pitch <= 2)  return 0;   // C4 D4 E4
  if (pitch <= 5)  return 1;   // F4 G4 A4
  if (pitch <= 8)  return 2;   // B4 C5 D5
  return 3;                    // E5 F5 G5 A5
}

static void loadCurrentSong() {
  int idx = game.currentSong;
  game.noteCount = 0;
  for (int i = 0; i < songLengths[idx]; i++) {
    NoteProto np;
    memcpy_P(&np, &songData[idx][i], sizeof(NoteProto));
    if (np.tick == 0xFFFFFFFF) break;
    game.pool[game.noteCount].tick = np.tick;
    game.pool[game.noteCount].pitch = np.pitch;
    game.pool[game.noteCount].lane = pitchToLane(np.pitch);
    game.pool[game.noteCount].type = np.type;
    game.pool[game.noteCount].duration = np.duration;
    game.pool[game.noteCount].active = true;
    game.pool[game.noteCount].sounded = false;
    game.noteCount++;
  }
  // 从 PROGMEM 读取歌曲名指针
  const char* namePtr = (const char*)pgm_read_ptr(&songNames[idx]);
  strncpy(game.currentSongName, namePtr, sizeof(game.currentSongName)-1);
  game.currentSongName[sizeof(game.currentSongName)-1] = '\0';
}

static void resetGame() {
  game.perfect = game.good = game.bad = game.miss = 0;
  game.combo = game.maxCombo = 0;
  game.acc = 100.0;
  game.gameOver = false;
  game.paused = false;
  game.activeCnt = 0;
  game.nextLoadIdx = 0;
  game.lastHitType = -1;
  game.lastHitTime = 0;
  game.lastKeyLane = -1;
  game.lastKeyTime = 0;
  for (int i=0; i<4; i++) game.keyState[i] = false;
  loadCurrentSong();
}

static void loadActiveNotes(uint32_t now) {
  // 提前 APPROACH_MS 加载, 使音符有完整下落过程(从顶部到判定线)
  while (game.nextLoadIdx < game.noteCount &&
         game.pool[game.nextLoadIdx].tick <= now + (uint32_t)APPROACH_MS) {
    if (game.activeCnt >= MAX_NOTES_ON_SCREEN) break;
    int idx = game.nextLoadIdx;
    int lane = game.pool[idx].lane;
    game.active[game.activeCnt].poolIdx = idx;
    game.active[game.activeCnt].x = lane * LANE_WIDTH + 2;
    game.active[game.activeCnt].y = 0;
    game.active[game.activeCnt].holdActive = false;
    game.active[game.activeCnt].holdJudge = -1;
    game.active[game.activeCnt].pressTime = 0;
    game.activeCnt++;
    game.nextLoadIdx++;
  }
}

static void updateNotesPosition(uint32_t now) {
  int64_t gameTime = (int64_t)now - (int64_t)game.gameStartTime;
  for (int i = 0; i < game.activeCnt; ) {
    RuntimeNote& note = game.pool[game.active[i].poolIdx];
    // 音符在 tick 时刻正好落到判定线; 之前在上方, 之后继续下落
    float yf = JUDGE_LINE_Y + (float)(gameTime - (int64_t)note.tick) * PX_PER_MS;
    if (yf > LCD_HEIGHT + NOTE_HEIGHT) {
      if (!note.active) {
        game.active[i] = game.active[--game.activeCnt];
        continue;
      }
      game.miss++;
      game.combo = 0;
      computeACC();
      setHitFeedback(3);
      note.active = false;
      game.active[i] = game.active[--game.activeCnt];
      continue;
    }
    if (yf < 0) yf = 0;
    game.active[i].y = (int)yf;
    // 音符到达判定线时自动发声(形成完整旋律), 无论玩家是否按中
    if (!note.sounded && gameTime >= (int64_t)note.tick) {
      note.sounded = true;
      playNoteDuration(note.pitch, (uint16_t)note.duration);
    }
    i++;
  }
}

static void processInput(uint32_t now, uint8_t key) {
  int lane = -1;
  switch(key) {
    case KEY_1: case KEY_LEFT:  lane = 0; break;
    case KEY_2: case KEY_UP:    lane = 1; break;
    case KEY_3: case KEY_DOWN:  lane = 2; break;
    case KEY_4: case KEY_RIGHT: lane = 3; break;
  }
  if (lane != -1) {
    if (!game.keyState[lane]) {
      game.keyState[lane] = true;
      game.lastKeyLane = lane;
      game.lastKeyTime = now;
      int bestIdx = -1, bestDist = 9999;
      for (int i = 0; i < game.activeCnt; i++) {
        RuntimeNote& note = game.pool[game.active[i].poolIdx];
        if (note.lane == lane && note.active) {
          int dist = abs(game.active[i].y - JUDGE_LINE_Y);
          if (dist < bestDist) { bestDist = dist; bestIdx = i; }
        }
      }
      if (bestIdx != -1) {
        RuntimeNote& note = game.pool[game.active[bestIdx].poolIdx];
        int delta = abs((int)((now - game.gameStartTime) - note.tick));
        if (delta <= BAD_MS) {
          int judgeType;
          if (delta <= PERFECT_MS) judgeType = 0;
          else if (delta <= GOOD_MS) judgeType = 1;
          else judgeType = 2;
          if (note.type == 0) {
            if (!note.sounded) { note.sounded = true; playNoteDuration(note.pitch, (uint16_t)note.duration); }
            if (judgeType == 0) game.perfect++;
            else if (judgeType == 1) game.good++;
            else game.bad++;
            game.combo = (judgeType <= 1) ? (game.combo + 1) : 0;
            if (game.combo > game.maxCombo) game.maxCombo = game.combo;
            computeACC();
            setHitFeedback(judgeType);
            note.active = false;
            game.active[bestIdx] = game.active[--game.activeCnt];
          } else {
            if (judgeType == 2) {
              game.bad++;
              game.combo = 0;
              computeACC();
              setHitFeedback(2);
              note.active = false;
              game.active[bestIdx] = game.active[--game.activeCnt];
            } else {
              game.active[bestIdx].holdActive = true;
              game.active[bestIdx].holdJudge = judgeType;
              game.active[bestIdx].pressTime = now;
              if (!note.sounded) { note.sounded = true; playNoteDuration(note.pitch, (uint16_t)note.duration); }
            }
          }
        } else {
          game.miss++;
          game.combo = 0;
          computeACC();
          setHitFeedback(3);
          note.active = false;
          game.active[bestIdx] = game.active[--game.activeCnt];
        }
      } else {
        game.combo = 0;
      }
    }
  } else {
    for (int l = 0; l < 4; l++) {
      if (game.keyState[l]) {
        uint8_t phyKey = KEY_NONE;
        switch(l) {
          case 0: phyKey = KEY_1; break;
          case 1: phyKey = KEY_2; break;
          case 2: phyKey = KEY_3; break;
          case 3: phyKey = KEY_4; break;
        }
        if (!isKeyPressed(phyKey)) {
          game.keyState[l] = false;
          for (int i = 0; i < game.activeCnt; i++) {
            RuntimeNote& note = game.pool[game.active[i].poolIdx];
            if (note.type == 1 && note.lane == l && game.active[i].holdActive) {
              uint32_t holdDuration = now - game.active[i].pressTime;
              int delta = abs((int)holdDuration - (int)note.duration);
              int finalJudge = game.active[i].holdJudge;
              if (delta > GOOD_MS) finalJudge = 2;
              else if (delta > PERFECT_MS) finalJudge = 1;
              if (finalJudge == 0) game.perfect++;
              else if (finalJudge == 1) game.good++;
              else game.bad++;
              if (finalJudge <= 1) game.combo++;
              else game.combo = 0;
              if (game.combo > game.maxCombo) game.maxCombo = game.combo;
              computeACC();
              setHitFeedback(finalJudge);
              note.active = false;
              game.active[i] = game.active[--game.activeCnt];
              break;
            }
          }
        }
      }
    }
  }
}

static void renderGame(uint32_t now) {
  lcdClear(COLOR_BLACK);

  // 轨道竖线
  for (int i = 1; i < NOTE_LANES; i++) {
    lcdDrawLine(i * LANE_WIDTH, 0, i * LANE_WIDTH, LCD_HEIGHT, 0x4208);
  }
  // 判定线
  lcdDrawLine(0, JUDGE_LINE_Y, LCD_WIDTH, JUDGE_LINE_Y, COLOR_WHITE);

  // 音符
  for (int i = 0; i < game.activeCnt; i++) {
    RuntimeNote& note = game.pool[game.active[i].poolIdx];
    int x = game.active[i].x;
    int y = game.active[i].y;
    if (note.type == 0) {
      lcdFillRect(x, y, LANE_WIDTH - 4, NOTE_HEIGHT, COLOR_WHITE);
    } else {
      // 长按条: 头部在 y(按下点), 尾巴向上延伸 duration 对应的像素
      int barLen = (int)(note.duration * PX_PER_MS);
      int topY = y - barLen;
      if (topY < 0) topY = 0;
      if (y > topY) lcdFillRect(x, topY, LANE_WIDTH - 4, y - topY, 0x8410);
    }
  }

  // HUD
  lcdSetTextColor(COLOR_WHITE, COLOR_BLACK);
  char buf[20];
  snprintf(buf, sizeof(buf), "%d", game.combo);
  lcdDrawString(LCD_WIDTH - 40, 4, buf, 2);
  lcdDrawString(2, 4, game.currentSongName, 1);

  lcdRefresh();
}

// 根据最近按键更新OLED表情
static void updateOLEDFace() {
  uint32_t now = millis();
  uint8_t face = FACE_IDLE_A;

  // 检测当前按住的按键组合 (全屏角色优先)
  bool k0 = game.keyState[0];  // 左一
  bool k1 = game.keyState[1];  // 左二
  bool k2 = game.keyState[2];  // 右一
  bool k3 = game.keyState[3];  // 右二

  if (game.lastKeyTime > 0 && now - game.lastKeyTime < 500) {
    bool leftAny  = (k0 || k1);
    bool rightAny = (k2 || k3);
    if (leftAny && rightAny) { oledShowConflict(); return; }  // 左右冲突
    if (k0 && k1)            { oledShowPhanion3(); return; }  // 左一+左二
    if (k2 && k3)            { oledShowMydei3();   return; }  // 右一+右二
    if (k0)                  { oledShowPhanion();  return; }  // 左一
    if (k1)                  { oledShowPhanion2(); return; }  // 左二
    if (k2)                  { oledShowMydei1();   return; }  // 右一
    if (k3)                  { oledShowMydei2();   return; }  // 右二
  }

  // 无全屏角色时: 命中反馈 / combo表情
  if (game.lastHitTime > 0 && now - game.lastHitTime < 400) {
    switch (game.lastHitType) {
      case 0: face = FACE_PIANO_COMBO; break;
      case 1: face = FACE_PIANO_1;     break;
      case 2: face = FACE_PIANO_MISS;  break;
      case 3: face = FACE_PIANO_MISS;  break;
    }
  } else if (game.combo >= 20) {
    face = FACE_PIANO_COMBO;
  } else if (game.combo >= 10) {
    face = FACE_PIANO_4;
  } else if (game.combo >= 5) {
    face = FACE_PIANO_2;
  }

  char top[16], bottom[16];
  snprintf(top, sizeof(top), "ACC:%.0f%%", game.acc);
  snprintf(bottom, sizeof(bottom), "P%dG%dB%dM%d",
           game.perfect, game.good, game.bad, game.miss);
  oledDrawBorderText(top, bottom);
  oledShowFace(face);
}

static void drawStartScreen() {
  lcdClear(COLOR_DARKBG);
  // 标题
  lcdFillRect(0, 0, LCD_WIDTH, 45, COLOR_NAVY);
  lcdSetTextColor(COLOR_CYAN, COLOR_NAVY);
  lcdDrawString(30, 8, "Piano Game", 2);
  // 曲目信息
  lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
  lcdDrawString(15, 70, "Song:", 2);
  lcdSetTextColor(COLOR_GOLD, COLOR_DARKBG);
  // 从 PROGMEM 读取歌曲名
  const char* namePtr = (const char*)pgm_read_ptr(&songNames[game.currentSong]);
  lcdDrawString(15, 100, namePtr, 2);
  // 操作提示
  lcdSetTextColor(0x8410, COLOR_DARKBG);
  lcdDrawString(15, 150, "Keys:", 1);
  lcdDrawString(15, 170, "1 2 3 4", 2);
  // 当前难度
  lcdSetTextColor(0x8410, COLOR_DARKBG);
  char db[24];
  snprintf(db, sizeof(db), "Difficulty: %s", DIFF_NAMES[pianoDifficulty]);
  lcdDrawString(15, 200, db, 1);
  lcdSetTextColor(0x8410, COLOR_DARKBG);
  lcdDrawString(15, 220, "5/6:Change Song", 1);
  lcdSetTextColor(COLOR_GOLD, COLOR_DARKBG);
  lcdDrawString(15, 245, "Auto start 3s...", 2);
  lcdRefresh();
}

// ========== 公共接口 ==========
// 难度选择界面
static void drawDiffScreen() {
  lcdClear(COLOR_DARKBG);
  lcdFillRect(0, 0, LCD_WIDTH, 50, COLOR_NAVY);
  lcdSetTextColor(COLOR_CYAN, COLOR_NAVY);
  lcdDrawString(25, 14, "Piano Game", 2);

  lcdSetTextColor(0x8410, COLOR_DARKBG);
  lcdDrawString(30, 75, "Select Difficulty", 1);

  const uint16_t cols[3] = {COLOR_GREEN, COLOR_GOLD, COLOR_RED};
  for (int i = 0; i < 3; i++) {
    int y = 105 + i * 50;
    lcdFillRect(30, y, 8, 36, cols[i]);
    lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
    char buf[20];
    snprintf(buf, sizeof(buf), "%d: %s", i + 1, DIFF_NAMES[i]);
    lcdDrawString(50, y + 8, buf, 2);
  }
  lcdSetTextColor(0x4208, COLOR_DARKBG);
  lcdDrawString(20, LCD_HEIGHT - 20, "1/2/3 select   * back", 1);
  lcdRefresh();
  oledDrawBorderText("Piano", "1/2/3 difficulty");
}

void pianoGameInit() {
  game.currentSong = 0;
  game.selectingDiff = true;     // 先进难度选择
  resetGame();
  stopTone();
  drawDiffScreen();
}

void pianoGameLoop(uint8_t key) {
  if (key == KEY_BACK) {
    stopTone();
    currentMode = MODE_MENU;
    drawMenu();
    oledShowWelcome();
    return;
  }

  // 难度选择阶段: 按 1/2/3 选难度后开始
  if (game.selectingDiff) {
    int d = -1;
    if (key == KEY_1) d = 0;
    else if (key == KEY_2) d = 1;
    else if (key == KEY_3) d = 2;
    if (d >= 0) {
      setDifficulty(d);
      game.selectingDiff = false;
      resetGame();
      game.gameStartTime = millis() + 3000;
      game.lastOledUpdate = 0;
      drawStartScreen();
      oledDrawBorderText("Piano Game", "Press 5/6");
    }
    return;   // 选难度阶段不处理其它键
  }

  if (key == KEY_5) {
    game.currentSong = (game.currentSong + 1) % MAX_SONGS;
    resetGame();
    game.gameStartTime = millis() + 3000;
    drawStartScreen();
    return;
  }
  if (key == KEY_6) {
    game.currentSong = (game.currentSong - 1 + MAX_SONGS) % MAX_SONGS;
    resetGame();
    game.gameStartTime = millis() + 3000;
    drawStartScreen();
    return;
  }
  if (key == KEY_ENTER && !game.gameOver) {
    game.paused = !game.paused;
    if (game.paused) {
      stopTone();
      lcdClear(COLOR_DARKBG);
      lcdFillRect(0, 120, LCD_WIDTH, 50, COLOR_NAVY);
      lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
      lcdDrawString(50, 130, "PAUSED", 3);
      lcdSetTextColor(COLOR_GRAY, COLOR_DARKBG);
      lcdDrawString(30, 220, "ENTER to resume", 2);
      lcdRefresh();
    }
    return;
  }
  if (game.paused) return;

  uint32_t now = millis();
  uint32_t gameTime = (now >= game.gameStartTime) ? (now - game.gameStartTime) : 0;

  loadActiveNotes(gameTime);
  updateNotesPosition(now);
  processInput(now, key);

  if (game.nextLoadIdx >= game.noteCount && game.activeCnt == 0 && !game.gameOver) {
    game.gameOver = true;
  }

  if (game.gameOver) {
    lcdClear(COLOR_BLACK);
    lcdSetTextColor(COLOR_WHITE, COLOR_BLACK);
    // 结算画面
    lcdClear(COLOR_DARKBG);
    lcdFillRect(0, 0, LCD_WIDTH, 40, COLOR_NAVY);
    lcdSetTextColor(COLOR_GOLD, COLOR_NAVY);
    lcdDrawString(45, 5, "GAME OVER", 3);

    char grade = 'D';
    if      (game.acc >= 95.0f) grade = 'S';
    else if (game.acc >= 85.0f) grade = 'A';
    else if (game.acc >= 70.0f) grade = 'B';
    else if (game.acc >= 55.0f) grade = 'C';
    char gs[4] = {grade, 0};
    lcdSetTextColor(COLOR_GOLD, COLOR_DARKBG);
    lcdDrawString(LCD_WIDTH - 50, 45, gs, 4);

    char buf[40];
    lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
    snprintf(buf, sizeof(buf), "Song: %s", game.currentSongName);
    lcdDrawString(10, 50, buf, 1);
    snprintf(buf, sizeof(buf), "ACC: %.1f%%", game.acc);
    lcdSetTextColor(COLOR_CYAN, COLOR_DARKBG);
    lcdDrawString(10, 75, buf, 2);
    lcdSetTextColor(COLOR_WHITE, COLOR_DARKBG);
    snprintf(buf, sizeof(buf), "Combo: %d", game.maxCombo);
    lcdDrawString(10, 110, buf, 2);
    lcdSetTextColor(COLOR_GOLD, COLOR_DARKBG);
    snprintf(buf, sizeof(buf), "P:%d", game.perfect);
    lcdDrawString(10, 145, buf, 2);
    lcdSetTextColor(COLOR_GREEN, COLOR_DARKBG);
    snprintf(buf, sizeof(buf), "G:%d", game.good);
    lcdDrawString(80, 145, buf, 2);
    lcdSetTextColor(COLOR_ORANGE, COLOR_DARKBG);
    snprintf(buf, sizeof(buf), "B:%d", game.bad);
    lcdDrawString(150, 145, buf, 2);
    lcdSetTextColor(COLOR_RED, COLOR_DARKBG);
    snprintf(buf, sizeof(buf), "M:%d", game.miss);
    lcdDrawString(10, 180, buf, 2);
    lcdDrawLine(10, 220, LCD_WIDTH - 10, 220, 0x4208);
    lcdSetTextColor(COLOR_GRAY, COLOR_DARKBG);
    lcdDrawString(40, 250, "ENTER to retry", 2);

    lcdRefresh();

    oledDrawBorderText("Game Over", "ENTER=retry");
    oledShowBlinkingFace(FACE_DONE_A, FACE_DONE_B, 500);

    if (key == KEY_ENTER) {
      resetGame();
      game.gameOver = false;
      game.gameStartTime = millis() + 1000;
    }
    return;
  }

  renderGame(now);
  if (now - game.lastOledUpdate >= 80) {
    updateOLEDFace();
    game.lastOledUpdate = now;
  }
}

void pianoGameNextSong() {
  game.currentSong = (game.currentSong + 1) % MAX_SONGS;
  resetGame();
  game.gameStartTime = millis() + 1000;
}

void pianoGamePrevSong() {
  game.currentSong = (game.currentSong - 1 + MAX_SONGS) % MAX_SONGS;
  resetGame();
  game.gameStartTime = millis() + 1000;
}
