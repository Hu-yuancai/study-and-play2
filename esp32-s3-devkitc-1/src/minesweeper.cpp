/*
 * minesweeper.cpp — 扫雷游戏 (Minesweeper)
 * ============================================================================
 * 【新手入门——这个文件做什么？】
 * 这是扫雷游戏的完整实现。它负责：
 *   1. 在 LCD 上画 N×M 方格网格
 *   2. 随机布雷（首次点击保证不踩雷）
 *   3. 用键盘移动光标、揭开格子、插旗标记
 *   4. 判定输赢 + 显示结果
 *   5. 通过 OLED 副屏显示表情反馈
 *   6. 从 safeRAM 读取参数（可被 Ghost Shell 实时修改）
 *   7. 用 Preferences 保存最佳成绩
 *
 * 【扫雷规则（给没玩过的人）】
 *   格子下面可能藏着地雷。你的目标是揭开所有"没有雷"的格子。
 *   - 揭开格子 → 如果下面是雷 → 游戏失败
 *   - 揭开格子 → 如果不是雷 → 显示数字（0-8），表示周围 8 格中有几颗雷
 *   - 如果数字是 0 → 自动展开周围所有安全区域
 *   - 你可以在怀疑有雷的格子上"插旗"标记
 *   - 揭完所有非雷格子 → 胜利！
 *
 * 【坐标系统】
 *   (0,0) = 左上角第一格
 *   X 向右增大，Y 向下增大
 *   grid[y * width + x] 访问格子（一维数组模拟二维）
 */

#include "minesweeper.h"
#include "game_common.h"   // currentMode(当前游戏模式), drawMenu(), deltaTime
#include "lcd_driver.h"   // LCD绘图: lcdFillRect, lcdDrawString, lcdRefresh 等
#include "oled_driver.h"  // OLED表情: oledShowFace, oledShowBlinkingFace 等
#include "audio.h"        // 音效: playSFX_success, playSFX_explode, playNote
#include "safe_ram.h"     // 安全内存: safeRAM[地址] 读写扫雷参数
#include <Arduino.h>
#include <Preferences.h>  // ESP32 非易失存储: 保存最佳成绩到 Flash

// ============================================================================
// 显示参数 (改这些值可以调整棋盘外观)
// ============================================================================

/*
 * CELL_SIZE: 每个格子的像素大小
 * 默认 24px → 12×12 棋盘 = 288×288 像素 (居中于 320×240 屏幕)
 * 如果改成 20px → 16×16 棋盘 = 320×320 像素 (刚好占满宽度)
 * 【改小】→ 格子更密, 棋盘更大   【改大】→ 格子更疏, 棋盘更小
 */
#define CELL_SIZE  24

/*
 * MS_MAX_CELLS: 一维数组最大容量
 * 16×16=256, 所以取 256 够用
 * 如果以后想支持更大的棋盘, 改成更大的值即可
 */
#define MS_MAX_CELLS 256

/*
 * GRID_OFFSET_X / GRID_OFFSET_Y: 棋盘在屏幕上的起始位置
 * 计算: (320 - CELL_SIZE*12) / 2 = 16 → 水平居中
 * Y 偏移 48 给顶部信息栏留空间
 */
#define GRID_OFFSET_X 16
#define GRID_OFFSET_Y 48

// ============================================================================
// 格子数据结构
// ============================================================================

/*
 * Cell: 一个格子的所有信息
 *
 * isMine:   这个格子下面是否埋了地雷（true=有雷）
 * revealed: 是否已经被玩家揭开（true=已揭开）
 * flagged:  是否被玩家插了旗（true=有旗）
 * nearby:   周围8格中有几颗雷（0-8，只在 revealed 后有意义）
 *
 * 【内存占用】每个 Cell: 1+1+1+1 = 4 字节
 *   256 格 × 4 字节 = 约 1KB (完全可以忽略)
 */
struct Cell {
  bool    isMine;    // 是雷吗？
  bool    revealed;  // 揭开没？
  bool    flagged;   // 插旗没？
  uint8_t nearby;    // 周围有几颗雷？(0-8)
};

// ============================================================================
// 全局游戏状态 (ms = minesweeper state)
// ============================================================================

/*
 * 把所有游戏状态打包在一个 struct 里，好处：
 *   1. 命名空间：ms.xxx 一看就知道是扫雷的状态
 *   2. 方便重置：minesweeperInit() 只需重置这个结构体
 *   3. 不会和其他模块的变量名冲突
 *
 * 【动态网格】grid 是一维数组，用 grid[y * w + x] 访问
 *   这比固定 grid[16][16] 更灵活——棋盘大小可以从 safeRAM 动态读取
 */
static struct {
  Cell     grid[MS_MAX_CELLS];  // 格子数据 (一维数组, y*w+x 访问)
  uint8_t  w, h;                // 当前棋盘宽高 (8-16)
  uint8_t  totalMines;          // 总雷数
  uint8_t  safeZone;            // 首次点击安全区大小 (简单=5, 中=3, 难=3)
  int      cx, cy;              // 光标位置 (玩家当前选中的格子坐标)
  bool     firstClick;          // 是否还没点第一下 (true=等待首次点击)
  bool     gameOver;            // 游戏结束标志
  bool     won;                 // 是否胜利
  bool     cheatReveal;         // 作弊透视开关 (从 safeRAM[0x24] 读取)
  unsigned long startTime;      // 游戏开始时间戳 (首次点击时记录)
  int      revealedCount;       // 已揭开格子数 (用于判胜)
} ms;

// ============================================================================
// 难度预设 (通过 Ghost Shell 的 Hack 菜单 → ENTER 直接输入自定义值)
// ============================================================================

/*
 * presets[难度][参数]: {w, h, mines, safeZone}
 *
 * safeZone: 首次点击时，以点击位置为中心的 safeZone×safeZone 区域内保证无雷
 *   简单模式 safeZone=5 意味着 5×5=25 格内绝对安全 → 首次点开一定是一片大空白
 *   中/困难 safeZone=3 → 3×3=9 格内安全
 *   【改难度】直接改下面数组的值即可
 */
static const uint8_t presets[3][4] = {
  {8,  8,  10, 5},   // 简单: 8×8,  10雷, 5×5安全区
  {12, 12, 20, 3},   // 中等: 12×12, 20雷, 3×3安全区 (默认)
  {16, 16, 40, 3}    // 困难: 16×16, 40雷, 3×3安全区
};
static int difficulty = 1;  // 0=简单, 1=中等, 2=困难

// ============================================================================
// Preferences 存档 (成绩保存在 ESP32 Flash 中, 断电不丢失)
// ============================================================================

/*
 * Preferences 是 ESP32 的非易失存储库。类似电脑的"注册表"或"ini文件"。
 * 数据写入 Flash，断电后仍然保留。每个键值对有一个名字(字符串)和一个值(整数)。
 *
 * 存储内容:
 *   bt0/bt1/bt2 → 简单/中等/困难的最佳时间(秒)
 *   wins/games  → 总胜场 / 总局数
 *
 * 【改键名】修改 "bt0" 等字符串即可。注意: 键名最长15字符。
 * 【清除存档】把 ESP32 重新烧录即可，或者调用 prefs.clear()
 */
static Preferences prefs;                     // Preferences 对象
static int bestTimes[3] = {0, 0, 0};          // 每难度的最佳时间(秒), 0=无记录
static int totalWins  = 0;                     // 总胜场
static int totalGames = 0;                     // 总局数

// 从 Flash 读取存档 (true=只读模式, 安全)
static void loadPrefs() {
  prefs.begin("minesweeper", true);    // 打开命名空间"minesweeper", true=只读
  bestTimes[0] = prefs.getInt("bt0", 0);  // 读键"bt0", 默认值0
  bestTimes[1] = prefs.getInt("bt1", 0);
  bestTimes[2] = prefs.getInt("bt2", 0);
  totalWins    = prefs.getInt("wins", 0);
  totalGames   = prefs.getInt("games", 0);
  prefs.end();                         // 关闭 (必须调用, 否则内存泄漏)
}

// 写入 Flash (false=读写模式)
static void savePrefs() {
  prefs.begin("minesweeper", false);   // false=读写模式
  prefs.putInt("bt0", bestTimes[0]);
  prefs.putInt("bt1", bestTimes[1]);
  prefs.putInt("bt2", bestTimes[2]);
  prefs.putInt("wins", totalWins);
  prefs.putInt("games", totalGames);
  prefs.end();
}

// ============================================================================
// 网格访问辅助函数
// ============================================================================

/*
 * cell(x, y): 用坐标(x,y)访问格子
 * 内部计算: index = y * width + x
 *
 * 例如: 8×8 棋盘, 格子(3, 2) → index = 2*8 + 3 = 19
 *   (0,0)(1,0)...(7,0) = 索引 0-7
 *   (0,1)(1,1)...(7,1) = 索引 8-15
 *   (0,2)(1,2)...       = 索引 16-23  ← (3,2) 在这里
 *
 * 【返回引用 &】意味着你可以直接赋值: cell(3,2).revealed = true;
 */
static Cell& cell(int x, int y) {
  return ms.grid[y * ms.w + x];
}

/*
 * inBounds(x, y): 检查坐标是否在棋盘范围内
 * 防止访问 grid 数组越界导致崩溃
 */
static bool inBounds(int x, int y) {
  return x >= 0 && x < ms.w && y >= 0 && y < ms.h;
}

// ============================================================================
// 布雷算法
// ============================================================================

/*
 * countNearby(x, y): 统计格子(x,y)周围8格中有几颗雷
 *
 * 双重循环遍历 3×3 邻域:
 *   dy = -1, 0, 1 (上一行, 当前行, 下一行)
 *     dx = -1, 0, 1 (左一列, 当前列, 右一列)
 * 跳过中心 (dx==0 && dy==0)
 * 跳过越界的格子 (通过 inBounds 检查)
 */
static int countNearby(int x, int y) {
  int n = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if ((dx || dy)                        // 不是中心格 (至少dx或dy不为0)
          && inBounds(x + dx, y + dy)       // 不越界
          && cell(x + dx, y + dy).isMine)   // 是雷
        n++;
  return n;
}

/*
 * placeMines(safeX, safeY): 随机布雷
 *
 * 【首次点击保护】在世界上的扫雷游戏中，第一次点击永远不会踩雷。
 * 实现方式：在玩家第一次点击后，以点击位置为中心的 safeZone×safeZone
 * 区域内不放雷。这个区域被称为"安全区"。
 *
 * 【算法步骤】
 * 1. 在安全区之外的格子中，随机选 totalMines 个格子放雷
 * 2. 放完雷后，遍历所有非雷格子，计算它们的 nearby 值 (周围雷数)
 *
 * 【性能】最坏情况：只剩安全区可放雷时，random() 不断重试。
 * 但安全区通常很小(5×5=25)，棋盘至少64格，所以几乎不会重试太多次。
 */
static void placeMines(int safeX, int safeY) {
  int total = ms.w * ms.h;   // 总格子数
  int toPlace = ms.totalMines;

  while (toPlace > 0) {
    int idx = random(total);           // 随机索引 0 ~ total-1
    int x = idx % ms.w;                // 索引→X坐标: 余数
    int y = idx / ms.w;                // 索引→Y坐标: 商

    // 检查是否在安全区内 (首次点击 protection zone)
    bool inSafe = abs(x - safeX) <= ms.safeZone / 2
               && abs(y - safeY) <= ms.safeZone / 2;

    // 只有"不在安全区"且"不是已经放过雷"的格子才能放雷
    if (!cell(x, y).isMine && !inSafe) {
      cell(x, y).isMine = true;        // 放一颗雷
      toPlace--;                        // 还需放 toPlace-1 颗
    }
  }

  // 放完雷后，为所有非雷格子计算周围雷数
  for (int y = 0; y < ms.h; y++)
    for (int x = 0; x < ms.w; x++)
      if (!cell(x, y).isMine)
        cell(x, y).nearby = countNearby(x, y);
}

// ============================================================================
// BFS 展开空白区 (扫雷核心算法)
// ============================================================================

/*
 * revealArea(sx, sy): 从(sx, sy)开始，自动揭开所有相邻的安全区域
 *
 * 【为什么不用递归？】
 * 递归展开空白区是最直观的做法，但 ESP32 栈只有 8KB。
 * 极端情况(16×16 全空白棋盘)下，递归深度可能达到 256 层，每层约 40 字节
 * → 256×40 = 10KB > 8KB 栈 → 栈溢出崩溃！
 *
 * 【BFS (广度优先搜索) 队列方案】
 * 使用一个固定大小的队列数组 (256个元素)，手动管理队列头尾指针。
 * 每次从队列头部取出一个格子；如果它是空白(周围雷数=0)，
 * 就把它的 8 个邻居加入队列尾部。循环直到队列为空。
 *
 * 【为什么安全】队列在全局静态内存中分配，不占用栈空间。
 *   最坏情况 256 个元素 × 2个int × 2字节 = 1024 字节 (完全安全)
 *
 * 【可调参数】以下值可以改:
 *   队列最大容量: MS_MAX_CELLS (默认256)
 */
static void revealArea(int sx, int sy) {
  // 队列: qx[]存储X坐标, qy[]存储Y坐标
  static int qx[MS_MAX_CELLS], qy[MS_MAX_CELLS];
  int head = 0, tail = 0;    // head=取出位置, tail=放入位置

  // 第1步: 把起始格子放入队列
  qx[tail] = sx;
  qy[tail] = sy;
  tail++;

  // 第2步: 循环处理队列, 直到队列为空 (head == tail)
  while (head < tail) {
    int x = qx[head], y = qy[head];  // 取出队首格子
    head++;

    if (!inBounds(x, y)) continue;   // 越界保护

    Cell& c = cell(x, y);

    // 已揭开 / 有旗 / 是雷 → 跳过
    if (c.revealed || c.flagged || c.isMine) continue;

    // 揭开当前格子
    c.revealed = true;
    ms.revealedCount++;              // 计数器+1 (用于胜利判定)

    // 如果当前格子周围雷数为0 (空白格), 把8个邻居加入队列
    if (c.nearby == 0) {
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if ((dx || dy) && inBounds(x + dx, y + dy)) {
            Cell& nc = cell(x + dx, y + dy);
            // 只加入"未揭开+没旗+非雷"的格子
            if (!nc.revealed && !nc.flagged && !nc.isMine) {
              qx[tail] = x + dx;
              qy[tail] = y + dy;
              tail++;
              // 队列溢出保护 (理论上不会超过 MS_MAX_CELLS)
              if (tail >= MS_MAX_CELLS) { head = tail; break; }
            }
          }
        }
      }
    }
  }
}

// ============================================================================
// 胜负判定
// ============================================================================

/*
 * checkWin(): 检查是否胜利
 *
 * 胜利条件: 已揭开格子数 >= 总格子数 - 总雷数
 *
 * 例如: 12×12=144 格, 20 颗雷 → 需要揭开 144-20=124 个格子
 *
 * 胜利时: 记录最佳时间, 写入存档, 播放成功音效
 */
static void checkWin() {
  if (ms.revealedCount >= ms.w * ms.h - ms.totalMines) {
    ms.won = true;
    ms.gameOver = true;
    safeRAM[SAFE_MS_STATE] = 1;      // 更新 safeRAM 状态供 Ghost Shell 读取

    // 计算用时 (毫秒→秒)
    int t = ms.startTime ? (millis() - ms.startTime) / 1000 : 0;

    // 如果是新纪录(或首次记录), 更新最佳时间
    if (t < bestTimes[difficulty] || bestTimes[difficulty] == 0) {
      bestTimes[difficulty] = t;
    }

    totalWins++;
    totalGames++;
    savePrefs();                      // 写入 Flash
    playSFX_success();                // 播放"成功"旋律
  }
}

// ============================================================================
// LCD 渲染 (画棋盘)
// ============================================================================

static void render() {
  lcdClear(COLOR_BLACK);  // 清空画布 (黑底)

  // ── 顶部信息栏 (蓝色背景, 44像素高) ──
  lcdFillRect(0, 0, LCD_WIDTH, 44, COLOR_NAVY);
  lcdSetTextColor(COLOR_WHITE, COLOR_NAVY);

  // 显示剩余雷数 (总雷数 - 已插旗数)
  // 因为: 旗子 = 玩家认为有雷的格子, 剩余 = 总雷 - 已标
  int displayed = ms.totalMines;
  for (int y = 0; y < ms.h; y++)
    for (int x = 0; x < ms.w; x++)
      if (cell(x, y).flagged) displayed--;

  char buf[32];
  snprintf(buf, sizeof(buf), "Mines:%d", displayed);
  lcdDrawString(4, 4, buf);

  // 显示计时器 (秒)
  int sec = 0;
  if (!ms.firstClick && ms.startTime) {
    sec = ms.gameOver
      ? (ms.won ? bestTimes[difficulty] : 0)   // 胜利显示最佳时间, 失败显示0
      : (millis() - ms.startTime) / 1000;       // 游戏中实时计时
  }
  snprintf(buf, sizeof(buf), "Time:%d", sec);
  lcdDrawString(90, 4, buf);

  // 状态文字 (WIN! / LOSE)
  const char* status = ms.won ? "WIN!" : (ms.gameOver ? "LOSE" : "");
  lcdSetTextColor(ms.won ? COLOR_GREEN : COLOR_RED, COLOR_NAVY);
  lcdDrawString(170, 4, status);

  // 作弊模式提示
  if (!ms.gameOver && ms.cheatReveal) {
    lcdSetTextColor(COLOR_YELLOW, COLOR_NAVY);
    lcdDrawString(200, 4, "CHEAT");
  }

  // 失败时底部提示
  if (ms.gameOver && !ms.won) {
    lcdSetTextColor(COLOR_GRAY, COLOR_BLACK);
    lcdDrawString(60, LCD_HEIGHT - 20, "ENTER:retry BACK:menu", 1);
  }

  // ── 绘制棋盘网格 ──
  for (int y = 0; y < ms.h; y++) {
    for (int x = 0; x < ms.w; x++) {
      int px = GRID_OFFSET_X + x * CELL_SIZE;  // 格子左上角 X 像素
      int py = GRID_OFFSET_Y + y * CELL_SIZE;  // 格子左上角 Y 像素
      Cell& c = cell(x, y);

      // --- 光标高亮 (绿色双线框) ---
      if (x == ms.cx && y == ms.cy && !ms.gameOver) {
        lcdDrawRect(px - 1, py - 1, CELL_SIZE + 2, CELL_SIZE + 2, COLOR_GREEN);
        lcdDrawRect(px - 2, py - 2, CELL_SIZE + 4, CELL_SIZE + 4, COLOR_GREEN);
      }

      // --- 已揭开的格子 (或游戏结束后显示所有雷) ---
      if (c.revealed || (ms.gameOver && c.isMine) || (ms.cheatReveal && c.isMine)) {
        if (c.isMine) {
          // 地雷: 红底 + 白字 X (踩中的那颗用全红高亮)
          uint16_t bg = (ms.gameOver && x == ms.cx) ? COLOR_RED : 0xF800;
          lcdFillRect(px + 1, py + 1, CELL_SIZE - 2, CELL_SIZE - 2, bg);
          lcdSetTextColor(COLOR_WHITE, bg);
          lcdDrawString(px + 7, py + 3, ms.gameOver ? "X" : "*", 2);
        } else if (c.nearby > 0) {
          // 数字格: 浅灰底 + 彩色数字
          // 颜色方案: 1=蓝 2=绿 3=红 4=深蓝 5=暗红 6=青 7=黑 8=灰
          lcdFillRect(px + 1, py + 1, CELL_SIZE - 2, CELL_SIZE - 2, 0xC618);
          static const uint16_t numColors[9] = {
            0, 0x001F, 0x0400, 0xF800, 0x0010, 0x8000, 0x0400, 0x0000, 0x8410
          };
          lcdSetTextColor(numColors[c.nearby], 0xC618);
          char nb[2] = { (char)('0' + c.nearby), 0 };   // 数字→字符: 3→'3'
          lcdDrawString(px + 7, py + 3, nb, 2);
        } else {
          // 空白格 (周围雷数=0): 浅灰底, 无数字
          lcdFillRect(px + 1, py + 1, CELL_SIZE - 2, CELL_SIZE - 2, 0xC618);
        }
      } else {
        // --- 未揭开格子 ---
        lcdFillRect(px + 1, py + 1, CELL_SIZE - 2, CELL_SIZE - 2, 0x8410);   // 灰色填充
        lcdDrawRect(px, py, CELL_SIZE, CELL_SIZE, 0x632C);                    // 深灰边框

        if (c.flagged) {
          // 小红旗标志 (简易绘制)
          lcdFillRect(px + 7, py + 3, 8, 12, COLOR_RED);      // 红色旗面
          lcdDrawLine(px + 7, py + 3, px + 7, py + 14, COLOR_BLACK);  // 旗杆
          lcdDrawLine(px + 15, py + 5, px + 15, py + 10, COLOR_BLACK); // 横杆
        }
      }
    }
  }

  // ── 底部操作提示 ──
  if (!ms.gameOver) {
    lcdSetTextColor(0x4208, COLOR_BLACK);
    lcdDrawString(15, LCD_HEIGHT - 20, "Arrows:move ENTER:dig C:flag", 1);
  }

  lcdRefresh();  // 把画好的画面推到 LCD 屏幕
}

// ============================================================================
// OLED 副屏更新
// ============================================================================

static void updateOLED() {
  if (ms.won) {
    // 胜利 → 大笑眨眼表情
    oledDrawBorderText("You Win!", "");
    oledShowBlinkingFace(FACE_DONE_A, FACE_DONE_B, 500);
  } else if (ms.gameOver) {
    // 失败 → 哭脸
    oledDrawBorderText("Game Over", "");
    oledShowFace(FACE_PIANO_MISS);
  } else if (ms.cheatReveal) {
    // 作弊模式 → 墨镜酷脸
    oledShowFace(FACE_PIANO_5);
  } else {
    // 正常游戏中 → 待机眨眼
    char top[16], bot[16];
    snprintf(top, sizeof(top), "Mines:%d", ms.totalMines);
    int d = ms.firstClick ? 0 : (millis() - ms.startTime) / 1000;
    snprintf(bot, sizeof(bot), "Time:%d", d);
    oledDrawBorderText(top, bot);
    oledShowBlinkingFace(FACE_IDLE_A, FACE_IDLE_B, 1000);
  }
}

// ============================================================================
// 公共接口 (被 main.cpp 调用)
// ============================================================================

/*
 * minesweeperInit(): 初始化/重置游戏
 *
 * 调用时机:
 *   - 主菜单按 ENTER 进入扫雷时
 *   - 游戏中按 5 键重新开局时
 *   - 按 1/2/3 切换难度后
 *
 * 执行步骤:
 *   1. 加载存档 (最佳时间)
 *   2. 从 safeRAM 读取棋盘参数 (宽/高/雷数/作弊开关)
 *   3. 边界安全检查 (防止 Ghost Shell 写入非法值)
 *   4. 清空格子数据 + 重置状态
 *   5. 渲染界面
 */
void minesweeperInit() {
  loadPrefs();  // 从 Flash 读存档

  // --- 从 safeRAM 读取参数 (允许 Ghost Shell 动态修改) ---
  ms.w           = safeRAM[SAFE_MS_WIDTH];
  ms.h           = safeRAM[SAFE_MS_HEIGHT];
  ms.totalMines  = safeRAM[SAFE_MS_MINES];
  ms.safeZone    = 3;  // 默认 3×3 安全区
  ms.cheatReveal = safeRAM[SAFE_MS_CHEAT];

  // --- 边界保护 (即使 safeRAM 被写入了非法值也不会崩溃) ---
  if (ms.w < 8)  ms.w = 8;
  else if (ms.w > 16) ms.w = 16;
  if (ms.h < 8)  ms.h = 8;
  else if (ms.h > 16) ms.h = 16;

  int maxM = ms.w * ms.h * 0.4;   // 雷数上限 = 总格子数的 40%
  if (ms.totalMines < 1) ms.totalMines = 1;
  else if (ms.totalMines > maxM) ms.totalMines = maxM;

  // --- 重置游戏状态 ---
  memset(ms.grid, 0, sizeof(ms.grid));   // 清零所有格子 (std::memset)
  ms.cx = ms.w / 2;     // 光标初始在棋盘中心
  ms.cy = ms.h / 2;
  ms.firstClick     = true;
  ms.gameOver       = false;
  ms.won            = false;
  ms.startTime      = 0;
  ms.revealedCount  = 0;
  safeRAM[SAFE_MS_STATE] = 0;   // 状态=游戏中

  render();       // 画棋盘
  updateOLED();   // 更新副屏表情
}

/*
 * minesweeperLoop(key): 每帧调用, 处理一个按键事件
 *
 * @param key: 按下的键值 (KEY_UP, KEY_DOWN, ..., KEY_ENTER 等)
 *             由 main.cpp 的 loop() 从事件队列中取出后传入
 *
 * 按键功能:
 *   BACK     → 返回主菜单
 *   5        → 重新开局
 *   1/2/3    → 切换简单/中等/困难
 *   方向键    → 移动光标
 *   ENTER    → 揭开格子
 *   C (FIRE) → 插旗/取消旗子
 */
void minesweeperLoop(uint8_t key) {
  // ── 全局按键 ──
  if (key == KEY_BACK) {
    currentMode = MODE_MENU;
    drawMenu();
    oledShowWelcome();
    return;
  }
  if (key == KEY_5) {
    minesweeperInit();   // 重新开始
    return;
  }

  // ── 难度切换 (1=简单, 2=中等, 3=困难) ──
  int preset = -1;
  if (key == KEY_1)      preset = 0;
  else if (key == KEY_2) preset = 1;
  else if (key == KEY_3) preset = 2;

  if (preset >= 0) {
    difficulty = preset;
    ms.w = presets[preset][0];
    ms.h = presets[preset][1];
    ms.totalMines = presets[preset][2];
    ms.safeZone   = presets[preset][3];
    // 同步写入 safeRAM (让 Ghost Shell 能读到最新值)
    safeRAMPoke(SAFE_MS_WIDTH,  ms.w);
    safeRAMPoke(SAFE_MS_HEIGHT, ms.h);
    safeRAMPoke(SAFE_MS_MINES,  ms.totalMines);
    minesweeperInit();   // 用新参数重新开局
    return;
  }

  // ── 游戏已结束 → 只响应 ENTER (重来) ──
  if (ms.gameOver) {
    if (key == KEY_ENTER) { minesweeperInit(); return; }
    return;  // 忽略其他按键
  }

  // ── 光标移动 (边界限制). 扫雷用 A/B/C/D 移光标 (2/3 已用于难度切换) ──
  if (key == KEY_LEFT  && ms.cx > 0)        ms.cx--;
  if (key == KEY_RIGHT && ms.cx < ms.w - 1) ms.cx++;
  if (key == KEY_UP    && ms.cy > 0)        ms.cy--;
  if (key == KEY_DOWN  && ms.cy < ms.h - 1) ms.cy++;

  // ── 揭开格子 (ENTER 键) ──
  if (key == KEY_ENTER) {
    if (ms.firstClick) {
      // ===== 首次点击: 布雷 + 开始计时 =====
      ms.firstClick = false;
      placeMines(ms.cx, ms.cy);      // 以当前光标为安全区中心布雷
      ms.startTime = millis();        // 记录开始时间
      totalGames++;
      savePrefs();                     // 总局数+1 写入 Flash
    }

    Cell& c = cell(ms.cx, ms.cy);     // 获取光标所在格子

    if (!c.revealed && !c.flagged) {  // 只能揭开"未揭开+非旗子"的格子
      if (c.isMine) {
        // ===== 踩雷 → 游戏失败 =====
        c.revealed  = true;
        ms.gameOver = true;
        ms.won      = false;
        safeRAM[SAFE_MS_STATE] = 2;   // 状态=失败
        playSFX_explode();            // 爆炸音效
      } else {
        // ===== 安全 → BFS展开 + 判断胜利 =====
        revealArea(ms.cx, ms.cy);     // 自动展开安全区
        checkWin();                    // 检查是否赢了

        // 播放反馈音 (有数字=较高中音, 空白=低音)
        if (!ms.won && cell(ms.cx, ms.cy).revealed)
          playNote(cell(ms.cx, ms.cy).nearby > 0 ? 2 : 0);
      }
    }
  }

  // ── 插旗 / 取消旗子 (C 键 = KEY_FIRE) ──
  if (key == KEY_FIRE) {
    Cell& c = cell(ms.cx, ms.cy);
    if (!c.revealed) {
      c.flagged = !c.flagged;    // 翻转旗子状态 (插旗↔取消)
    }
  }

  // 每帧重新渲染 (因为光标/格子状态可能变化)
  render();
  updateOLED();
}
