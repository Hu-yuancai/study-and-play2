/*
 * ghost_shell.cpp — 十六进制内存编辑器 (纯键盘, 不连电脑)
 * ============================================================================
 * 【这是什么？】
 * Ghost Shell 是一个藏在游戏里的"黑客工具"。它让你:
 *   1. 像电影里的黑客一样浏览和修改内存(其实只是 64 字节的安全模拟区)
 *   2. 通过菜单快速修改扫雷/钢琴/显示参数, 不用记地址
 *   3. 直接输入任意数值(百十个位逐位编辑), 而不是只能+1/-1
 *
 * 【两个主要界面】
 *   内存编辑器 (默认): 十六进制 dump + 逐字节编辑
 *   Hack 菜单 (按7进入): 按参数名快速修改, 支持直接输入数值
 *
 * 【入口】主菜单 → ↑↑↓↓←→←→ 9 0 (屏幕无提示)
 *   LCD: 黑底绿字十六进制 dump
 *   OLED: 按键映射 / 地址信息
 *
 * 【安全机制】
 *   - 所有写入限制在 safeRAM[0..63] 范围内 (非真实内存)
 *   - 只读地址写入时蜂鸣器报警+拒绝
 *   - 超出参数范围的写入自动拒绝
 *   - 不影响系统稳定性 (ESP32 不会崩溃)
 *
 * 【给初学者——如何调试/修改】
 *   - 改 safeRAM 地址映射: 编辑 params[] 数组
 *   - 改 Hack 菜单项: 编辑 renderHack() / hack 按键处理
 *   - 改进入密码: 编辑 konami[] 数组 (main.cpp handleMenu 中)
 *   - 改界面颜色: 编辑 shellBg / shellFg 变量
 */
#include "ghost_shell.h"
#include "game_common.h"
#include "lcd_driver.h"
#include "oled_driver.h"
#include "audio.h"
#include "safe_ram.h"
#include "keyboard.h"
#include <Arduino.h>
#include <Preferences.h>

// ── 状态 ──
static uint16_t cursorAddr = 0;    // 0x00-0xFF
static int      baseMode  = 0;     // 0=HEX 1=DEC 2=BIN
static int      pageRow   = 0;     // 显示起始行 (0~15, 每行16字节)
static uint8_t  lastFind  = 0;     // 上次搜索的值

// 编辑模式
enum { EM_NONE, EM_EDIT, EM_GOTO, EM_FIND, EM_DUMP_S, EM_DUMP_E, EM_CONFIRM };
static int editMode = EM_NONE;
static uint8_t editVal = 0;
static int editNibble = 0;    // 当前编辑的位 (hex:0-1, bin:0-7)
static char inputBuf[8];
static int  inputPos = 0;
static int  confirmAction = 0; // 0=reset 1=exit

// ── 参数映射表 (地址→名称+类型+范围) ──
struct ParamInfo { const char* name; bool ro; uint8_t minV, maxV; };
static const ParamInfo params[SAFE_RAM_SIZE] PROGMEM = {
  // 0x00-0x0F
  {},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},
  // 0x10-0x1F 显示
  {"disp.bg",    false,0,6}, {"disp.font",   false,1,3},
  {},{},{},{},{},{},
  // 0x20-0x2F 扫雷
  {"ms.type",    true, 0,0}, {"ms.width",    false,8,16},
  {"ms.height",  false,8,16},{"ms.mines",    false,1,200},
  {"ms.cheat",   false,0,1}, {"ms.state",    true, 0,2},
  {},{},{},{},
  // 0x30-0x3F 钢琴
  {"pn.song",    false,0,5}, {"pn.scroll%",  false,50,200},
  {"pn.perfect", false,10,200},{"pn.autoplay",false,0,1},
  {"pn.nomiss",  false,0,1}, {"pn.bpm",      false,60,240},
};

// ── OLED 按键映射 ──
static void oledMap() {
  oledClear();
  oledDrawString(0,0,"1:edit 2:goto");
  oledDrawString(0,15,"3:base 4:find");
  oledDrawString(0,30,"5:save 6:reset");
  oledDrawString(0,45,"7:hack 0:help");
  oledRefresh();
}

// ── 数制转换 ──
static void fmtVal(char* buf, uint8_t v) {
  if (baseMode==1) snprintf(buf,16,"%d",v);
  else if (baseMode==2) { for(int i=7;i>=0;i--) buf[7-i]=(v>>i)&1?'1':'0'; buf[8]=0; }
  else snprintf(buf,16,"0x%02X",v);
}
static uint8_t parseVal(const char* s) {
  if (baseMode==2) { uint8_t v=0; for(int i=0;i<8&&s[i];i++) v=(v<<1)|(s[i]=='1'?1:0); return v; }
  else return strtol(s,NULL,baseMode==1?10:16);
}

// ── 渲染内存dump ──
static void renderDump() {
  lcdClear(0x0000);  // 黑底

  // 列标题
  lcdSetTextColor(0x0400, 0x0000);  // 暗绿
  lcdDrawString(0,0,"     0 1 2 3 4 5 6 7 8 9 A B C D E F",1);
  lcdDrawLine(0,10,LCD_WIDTH,10,0x0400);

  // 8行 × 16字节
  for (int row=0; row<8; row++) {
    int base = (pageRow + row) * 16;
    if (base >= SAFE_RAM_SIZE) break;
    int y = 12 + row*22;

    // 行号
    char buf[8];
    snprintf(buf,sizeof(buf),"%04X:", base);
    lcdSetTextColor(0x0400, 0x0000);
    lcdDrawString(0,y,buf,1);

    // 16字节
    for (int col=0; col<16; col++) {
      int addr = base + col;
      int x = 32 + col*18;
      bool isCursor = (addr == cursorAddr && editMode != EM_EDIT);

      uint8_t v = safeRAM[addr];

      // 高亮光标行
      if (isCursor) {
        lcdFillRect(x-1,y-1,16,12, COLOR_GREEN);
        lcdSetTextColor(0x0000, COLOR_GREEN);
      } else {
        lcdSetTextColor(COLOR_GREEN, 0x0000);
      }

      snprintf(buf,sizeof(buf),"%02X", v);
      lcdDrawString(x,y,buf,1);

      // ASCII 区
      if (col == 15) {
        // 最后一字节后画 ASCII
      }
    }
  }

  // ── 状态栏 ──
  lcdDrawLine(0,190,LCD_WIDTH,190,0x0400);
  lcdSetTextColor(COLOR_GREEN, 0x0000);
  char status[48];

  ParamInfo pi;
  memcpy_P(&pi, &params[cursorAddr], sizeof(ParamInfo));

  char vstr[16]; fmtVal(vstr, safeRAM[cursorAddr]);
  if (pi.name) {
    snprintf(status,sizeof(status),"ADR:0x%02X %s=%s %s",
      cursorAddr, pi.name, vstr, baseMode==0?"HEX":baseMode==1?"DEC":"BIN");
  } else {
    snprintf(status,sizeof(status),"ADR:0x%02X VAL:%s [%s] %s",
      cursorAddr, vstr, pi.ro?"RO":"RW",
      editMode==EM_EDIT?"EDITING":"");
  }
  lcdDrawString(4,194,status,1);

  // 快捷键栏
  lcdSetTextColor(0x0400,0x0000);
  lcdDrawString(2,212,"1ed 2go 3bs 4fd 5sv 6rs 7hk 0hp",1);
  lcdDrawString(2,226,"9exit BACK=back",1);

  lcdRefresh();
}

// ── 编辑模式渲染 ──
static void renderEdit() {
  lcdClear(0x0000);
  lcdFillRect(0,0,LCD_WIDTH,40,0x0400);
  lcdSetTextColor(0x0000,0x0400);
  lcdDrawString(10,8,"EDIT 0x",2);

  char buf[16];
  if (baseMode == 2) {
    // 二进制: 8位
    for (int i=0;i<8;i++) {
      int x=80+i*18;
      char bit[2]; bit[0]=((editVal>>(7-i))&1)?'1':'0'; bit[1]=0;
      bool on = (editNibble == i);
      lcdFillRect(x-1,7,16,22, on?COLOR_GREEN:0x0000);
      lcdSetTextColor(on?0x0000:COLOR_GREEN, on?COLOR_GREEN:0x0000);
      lcdDrawString(x,12,bit,2);
    }
  } else {
    // 十六进制: 2位
    snprintf(buf,sizeof(buf),"%02X", editVal);
    for (int i=0;i<2;i++) {
      int x=80+i*24;
      char d[2]; d[0]=buf[i]; d[1]=0;
      bool on = (editNibble == i);
      lcdFillRect(x-1,7,20,22, on?COLOR_GREEN:0x0000);
      lcdSetTextColor(on?0x0000:COLOR_GREEN, on?COLOR_GREEN:0x0000);
      lcdDrawString(x,12,d,2);
    }
  }
  lcdSetTextColor(COLOR_GREEN,0x0000);
  lcdDrawString(10,55,"LR:move UD:change",1);
  lcdDrawString(10,70,"ENTER:ok BACK:cancel",1);

  // 参数信息
  ParamInfo pi;
  memcpy_P(&pi, &params[cursorAddr], sizeof(ParamInfo));
  if (pi.name) {
    char info[40];
    snprintf(info,sizeof(info),"%s [%d-%d] %s", pi.name, pi.minV, pi.maxV, pi.ro?"RO":"RW");
    lcdDrawString(10,95,info,1);
  }
  lcdRefresh();
}

// ── 确认提示 ──
static void renderConfirm(const char* msg) {
  lcdClear(0x0000);
  lcdSetTextColor(COLOR_GREEN,0x0000);
  lcdDrawString(30,60,msg,2);
  lcdDrawString(40,100,"1=YES  2=NO",1);
  lcdRefresh();
}

// ── 输入界面 ──
static void renderInput(const char* prompt) {
  lcdClear(0x0000);
  lcdSetTextColor(COLOR_GREEN,0x0000);
  lcdDrawString(10,60,prompt,2);
  char buf[16]; snprintf(buf,sizeof(buf),"%s_", inputBuf);
  lcdDrawString(10,90,buf,2);
  lcdRefresh();
}

// ── Hack 菜单 ──
enum { HACK_MAIN, HACK_MS, HACK_PIANO, HACK_DISP };
static int hackMenu = HACK_MAIN;
static int hackSel = 0;
static bool hackInput = false;     // 数值直接输入模式
static uint8_t hackInputAddr = 0;  // 正在输入的地址
static uint8_t hackInputVal = 0;   // 正在输入的值 (0-255)
static int  hackInputPos = 0;      // 当前编辑的数位 (百位=0, 十位=1, 个位=2)
static const uint8_t hackDigitAddr[][3] PROGMEM = {}; // unused

static void drawHackTitle(const char* t) {
  lcdFillRect(0,0,LCD_WIDTH,36,COLOR_NAVY);
  lcdSetTextColor(COLOR_GREEN,COLOR_NAVY);
  lcdDrawString(5,6,t,2);
}
static void drawHackText(int i, int max, const char* s) {
  lcdSetTextColor(i==hackSel ? COLOR_WHITE : 0x0400, 0x0000);
  lcdDrawString(15, 55+i*28, s, 2);
}

static void renderHack() {
  lcdClear(0x0000);
  if (hackMenu == HACK_MAIN) {
    drawHackTitle("HACK MENU");
    const char* items[]={"Minesweeper","Piano Game","Display","[Back]"};
    for (int i=0;i<4;i++) drawHackText(i,4,items[i]);
    lcdSetTextColor(0x0400,0x0000);
    lcdDrawString(10,200,"ENTER:sel BACK:exit",1);
  }
  else if (hackMenu == HACK_MS) {
    drawHackTitle("MINESWEEPER");
    lcdSetTextColor(COLOR_GREEN,0x0000);
    char b[32]; int sel=hackSel;
    snprintf(b,sizeof(b),"Width:  %d",safeRAM[0x21]); drawHackText(0,7,b);
    snprintf(b,sizeof(b),"Height: %d",safeRAM[0x22]); drawHackText(1,7,b);
    snprintf(b,sizeof(b),"Mines:  %d",safeRAM[0x23]); drawHackText(2,7,b);
    snprintf(b,sizeof(b),"Reveal: %s",safeRAM[0x24]?"ON":"OFF"); drawHackText(3,7,b);
    snprintf(b,sizeof(b),"Force Win");  drawHackText(4,7,b);
    snprintf(b,sizeof(b),"Force Lose"); drawHackText(5,7,b);
    drawHackText(6,7,"[Back]");
    lcdSetTextColor(0x0400,0x0000);
    lcdDrawString(5,215,"LR:fine ENTER:type num",1);
  }
  else if (hackMenu == HACK_PIANO) {
    drawHackTitle("PIANO GAME");
    lcdSetTextColor(COLOR_GREEN,0x0000);
    char b[32]; int sel=hackSel;
    snprintf(b,sizeof(b),"Song:    %d",safeRAM[0x30]);     drawHackText(0,8,b);
    snprintf(b,sizeof(b),"Scroll%%: %d",safeRAM[0x31]);    drawHackText(1,8,b);
    snprintf(b,sizeof(b),"Perfect: %dms",safeRAM[0x32]);   drawHackText(2,8,b);
    snprintf(b,sizeof(b),"AutoPlay:%s",safeRAM[0x33]?"ON":"OFF"); drawHackText(3,8,b);
    snprintf(b,sizeof(b),"NoMiss:  %s",safeRAM[0x34]?"ON":"OFF"); drawHackText(4,8,b);
    snprintf(b,sizeof(b),"BPM:     %d",safeRAM[0x35]);     drawHackText(5,8,b);
    snprintf(b,sizeof(b),"Force Win"); drawHackText(6,8,b);
    drawHackText(7,8,"[Back]");
    lcdSetTextColor(0x0400,0x0000);
    lcdDrawString(5,215,"LR:fine ENTER:type num",1);
  }
  else if (hackMenu == HACK_DISP) {
    drawHackTitle("DISPLAY");
    lcdSetTextColor(COLOR_GREEN,0x0000);
    char b[32]; static const char* colorNames[]={"DARKBG","BLACK","BLUE","GREEN","YELLOW","RED","WHITE"};
    snprintf(b,sizeof(b),"BG:%s",colorNames[safeRAM[0x10]<=6?safeRAM[0x10]:0]); drawHackText(0,3,b);
    snprintf(b,sizeof(b),"Font:%dx",safeRAM[0x11]); drawHackText(1,3,b);
    drawHackText(2,3,"[Back]");
    lcdSetTextColor(0x0400,0x0000);
    lcdDrawString(5,215,"LR:change ENTER:type num",1);
  }
  lcdRefresh();
}

// ── 数值直接输入界面 ──
static void renderNumInput() {
  lcdClear(0x0000);
  lcdFillRect(0,0,LCD_WIDTH,40,0x0400);
  lcdSetTextColor(0x0000,0x0400);

  ParamInfo pi; memcpy_P(&pi, &params[hackInputAddr], sizeof(ParamInfo));
  char buf[32];
  snprintf(buf,sizeof(buf),"SET %s [%d-%d]", pi.name?pi.name:"?", pi.minV, pi.maxV);
  lcdDrawString(10,8,buf,2);

  // 大字体显示当前输入值 (3位十进制)
  lcdSetTextColor(COLOR_GREEN,0x0000);
  char digits[4]; snprintf(digits,sizeof(digits),"%03d", hackInputVal);
  for (int i=0;i<3;i++) {
    int x = 80 + i*36;
    char d[2]; d[0]=digits[i]; d[1]=0;
    bool on = (hackInputPos == i);
    lcdFillRect(x-2,52,30,36, on?COLOR_GREEN:0x0000);
    lcdSetTextColor(on?0x0000:COLOR_GREEN, on?COLOR_GREEN:0x0000);
    lcdDrawString(x+6,56,d,3);
  }

  lcdSetTextColor(0x0400,0x0000);
  lcdDrawString(40,105,"LR:move digit UD:change",1);
  lcdDrawString(40,120,"ENTER:confirm BACK:cancel",1);
  lcdRefresh();
}

static void startNumInput(uint8_t addr) {
  hackInput = true;
  hackInputAddr = addr;
  hackInputVal = safeRAM[addr];
  hackInputPos = 2;  // 从个位开始
  renderNumInput();
}

// ── 初始化 ──
void shellInit() {
  cursorAddr = 0; pageRow = 0; editMode = EM_NONE; baseMode = 0;
  hackMenu = HACK_MAIN; hackSel = 0;
  oledClear();
  oledDrawString(15,0,"GHOST SHELL");
  oledDrawString(5,20,"HACK MODE");
  oledDrawString(15,48,"v1.0");
  oledRefresh();
  playTone(800,50); delay(60);
  playTone(1000,50); delay(60);
  playTone(1200,80);
  renderDump();
  oledMap();
}

// ── 主循环 ──
void shellLoop(uint8_t key) {
  // ── 编辑模式 ──
  if (editMode == EM_EDIT) {
    if (key == KEY_BACK) { editMode = EM_NONE; renderDump(); oledMap(); return; }
    int maxNib = (baseMode==2)?7:1;
    if (key == KEY_LEFT  && editNibble>0)      editNibble--;
    if (key == KEY_RIGHT && editNibble<maxNib)  editNibble++;
    if (key == KEY_UP || key == KEY_DOWN) {
      int d = (key==KEY_UP)?1:-1;
      if (baseMode==2) editVal ^= (1<<(7-editNibble)); // 翻转位
      else {
        int v = (editVal>>((1-editNibble)*4))&0xF;
        v = (v+d+16)%16;
        if (editNibble==0) editVal = (editVal&0x0F)|(v<<4);
        else editVal = (editVal&0xF0)|v;
      }
    }
    if (key == KEY_ENTER) {
      // 范围检查
      ParamInfo pi; memcpy_P(&pi, &params[cursorAddr], sizeof(ParamInfo));
      if (pi.ro) { playTone(200,200); } // 只读拒绝
      else if (pi.name && (editVal < pi.minV || editVal > pi.maxV)) { playTone(200,200); }
      else {
        safeRAM[cursorAddr] = editVal;
        playTone(1200,25); playTone(1600,25);
      }
      editMode = EM_NONE; renderDump(); oledMap(); return;
    }
    renderEdit(); return;
  }

  // ── Goto 输入模式 ──
  if (editMode == EM_GOTO) {
    if (key == KEY_BACK) { editMode=EM_NONE; renderDump(); oledMap(); return; }
    if (key == KEY_ENTER) {
      if (inputPos>0) { inputBuf[inputPos]=0; cursorAddr = parseVal(inputBuf); if(cursorAddr>255)cursorAddr=255; pageRow=cursorAddr/16; }
      editMode=EM_NONE; renderDump(); oledMap(); return;
    }
    renderInput("GOTO addr:"); return;
  }

  // ── Find 输入模式 ──
  if (editMode == EM_FIND) {
    if (key == KEY_BACK) { editMode=EM_NONE; renderDump(); oledMap(); return; }
    if (key == KEY_ENTER) {
      if (inputPos>0) { inputBuf[inputPos]=0; lastFind = parseVal(inputBuf); }
      // 搜索
      bool found=false;
      for (int a=cursorAddr+1;a<256;a++) { if (safeRAM[a]==lastFind) { cursorAddr=a; pageRow=a/16; found=true; break; } }
      if (!found) {
        lcdClear(0x0000);
        lcdSetTextColor(COLOR_RED,0x0000);
        lcdDrawString(40,100,"Not found.",2);
        lcdRefresh(); delay(800);
      }
      editMode=EM_NONE; renderDump(); oledMap(); return;
    }
    renderInput("Find byte:"); return;
  }

  // ── 确认模式 ──
  if (editMode == EM_CONFIRM) {
    if (key == KEY_1) {
      if (confirmAction==0) { memset(safeRAM,0,SAFE_RAM_SIZE); safeRAMInit(); }
      else if (confirmAction==1) { currentMode=MODE_MENU; drawMenu(); oledShowWelcome(); return; }
      editMode=EM_NONE; renderDump(); oledMap(); return;
    }
    if (key == KEY_2) { editMode=EM_NONE; renderDump(); oledMap(); return; }
    return;
  }

  // ── Hack 菜单系统 (KEY_7 进入, BACK 返回编辑器) ──
  static bool inHack = false;

  // ── 数值直接输入模式 ──
  if (hackInput) {
    if (key == KEY_BACK) { hackInput=false; renderHack(); return; }
    if (key == KEY_LEFT)  { if(hackInputPos>0) hackInputPos--; }
    if (key == KEY_RIGHT) { if(hackInputPos<2) hackInputPos++; }
    if (key == KEY_UP || key == KEY_DOWN) {
      int d = (key==KEY_UP)?1:-1;
      int mul = 1; if (hackInputPos==0) mul=100; else if (hackInputPos==1) mul=10;
      int v = hackInputVal + d*mul;
      ParamInfo pi; memcpy_P(&pi, &params[hackInputAddr], sizeof(ParamInfo));
      if (v >= pi.minV && v <= pi.maxV) hackInputVal = v;
    }
    if (key == KEY_ENTER) {
      safeRAMPoke(hackInputAddr, hackInputVal);
      playTone(1200,25); delay(5); playTone(1600,25);
      hackInput = false; renderHack(); return;
    }
    renderNumInput();
    return;
  }

  if (inHack) {
    if (key == KEY_BACK) {
      if (hackMenu == HACK_MAIN) { inHack = false; renderDump(); oledMap(); return; }
      hackMenu = HACK_MAIN; hackSel = 0; renderHack(); return;
    }
    int maxS = (hackMenu==HACK_MAIN?4:(hackMenu==HACK_MS?7:(hackMenu==HACK_PIANO?8:3)));
    if (key == KEY_UP)   hackSel = (hackSel + maxS - 1) % maxS;
    if (key == KEY_DOWN) hackSel = (hackSel + 1) % maxS;

    // LR 精细调节 (±1)
    if (key == KEY_LEFT || key == KEY_RIGHT) {
      int d = (key == KEY_RIGHT) ? 1 : -1;
      if (hackMenu == HACK_MS) {
        if (hackSel==0) safeRAMPoke(0x21,safeRAM[0x21]+d);
        else if (hackSel==1) safeRAMPoke(0x22,safeRAM[0x22]+d);
        else if (hackSel==2) safeRAMPoke(0x23,safeRAM[0x23]+d);
      } else if (hackMenu == HACK_PIANO) {
        if (hackSel==0) safeRAMPoke(0x30,safeRAM[0x30]+d);
        else if (hackSel==1) safeRAMPoke(0x31,safeRAM[0x31]+(d*5));
        else if (hackSel==2) safeRAMPoke(0x32,safeRAM[0x32]+(d*5));
        else if (hackSel==5) safeRAMPoke(0x35,safeRAM[0x35]+(d*10));
      } else if (hackMenu == HACK_DISP) {
        if (hackSel==0) safeRAMPoke(0x10,safeRAM[0x10]+d);
        else if (hackSel==1) safeRAMPoke(0x11,safeRAM[0x11]+d);
      }
    }

    // ENTER: 数值参数→直接输入, 开关→切换, 动作→执行, 返回项→返回
    if (key == KEY_ENTER) {
      if (hackMenu == HACK_MAIN) {
        if (hackSel==0) { hackMenu=HACK_MS;   hackSel=0; }
        else if (hackSel==1) { hackMenu=HACK_PIANO; hackSel=0; }
        else if (hackSel==2) { hackMenu=HACK_DISP;  hackSel=0; }
        else if (hackSel==3) { inHack=false; renderDump(); oledMap(); return; }
      } else if (hackMenu == HACK_MS) {
        if (hackSel<=2) startNumInput(hackSel==0?0x21:(hackSel==1?0x22:0x23)); // width/height/mines
        else if (hackSel==3) safeRAMPoke(0x24,!safeRAM[0x24]); // toggle reveal
        else if (hackSel==4) { safeRAM[0x25]=1; playSFX_success(); }
        else if (hackSel==5) { safeRAM[0x25]=2; playSFX_explode(); }
        else if (hackSel==6) { hackMenu=HACK_MAIN; hackSel=0; }
      } else if (hackMenu == HACK_PIANO) {
        if (hackSel==0) startNumInput(0x30);      // song
        else if (hackSel==1) startNumInput(0x31);  // scroll%
        else if (hackSel==2) startNumInput(0x32);  // perfect ms
        else if (hackSel==3) safeRAMPoke(0x33,!safeRAM[0x33]); // autoplay
        else if (hackSel==4) safeRAMPoke(0x34,!safeRAM[0x34]); // nomiss
        else if (hackSel==5) startNumInput(0x35);  // BPM
        else if (hackSel==6) { safeRAMPoke(0x32,200); safeRAMPoke(0x34,1); playSFX_success(); }
        else if (hackSel==7) { hackMenu=HACK_MAIN; hackSel=0; }
      } else if (hackMenu == HACK_DISP) {
        if (hackSel==0) startNumInput(0x10);       // bg color index
        else if (hackSel==1) startNumInput(0x11);   // font size
        else if (hackSel==2) { hackMenu=HACK_MAIN; hackSel=0; }
      }
    }
    renderHack();
    return;
  }

  // ── 正常模式命令 ──
  if (key == KEY_BACK) {
    editMode=EM_CONFIRM; confirmAction=1;
    renderConfirm("Exit Shell?");
    return;
  }
  if (key == KEY_LEFT  && cursorAddr>0)   cursorAddr--;
  if (key == KEY_RIGHT && cursorAddr<255) cursorAddr++;
  if (key == KEY_UP)    { if (cursorAddr>=16) cursorAddr-=16; }
  if (key == KEY_DOWN)  { if (cursorAddr<240) cursorAddr+=16; }
  // 快速翻页
  if (key == KEY_FIRE && isKeyPressed(KEY_UP))   { if(cursorAddr>=128)cursorAddr-=128; else cursorAddr=0; }
  if (key == KEY_FIRE && isKeyPressed(KEY_DOWN)) { if(cursorAddr<128)cursorAddr+=128; else cursorAddr=255; }

  // 确保光标可见
  if (cursorAddr/16 < pageRow) pageRow = cursorAddr/16;
  if (cursorAddr/16 >= pageRow+8) pageRow = (cursorAddr/16)-7;
  if (pageRow<0) pageRow=0; if (pageRow>8) pageRow=8;

  // ── 数字命令 ──
  ParamInfo pi; memcpy_P(&pi, &params[cursorAddr], sizeof(ParamInfo));

  if (key == KEY_1) { // Edit
    if (pi.ro) { playTone(200,200); return; }
    editVal = safeRAM[cursorAddr]; editNibble=0; editMode=EM_EDIT;
    renderEdit(); return;
  }
  if (key == KEY_2) { // Goto
    editMode=EM_GOTO; inputPos=0; inputBuf[0]=0;
    renderInput("GOTO addr:"); return;
  }
  if (key == KEY_3) { // Base
    baseMode = (baseMode+1)%3;
    oledClear();
    oledDrawString(10,0,"Base:");
    oledDrawString(10,20,baseMode==0?"HEX":baseMode==1?"DEC":"BIN");
    oledRefresh(); delay(500); oledMap();
  }
  if (key == KEY_4) { // Find
    editMode=EM_FIND; inputPos=0; inputBuf[0]=0;
    renderInput("Find byte:"); return;
  }
  if (key == KEY_5) { // Save
    Preferences p; p.begin("saferam",false);
    p.putBytes("ram", safeRAM, 256); p.end();
    playSFX_success();
    lcdClear(0x0000);
    lcdSetTextColor(COLOR_GREEN,0x0000);
    lcdDrawString(50,100,"Saved.",2);
    lcdRefresh(); delay(600);
  }
  if (key == KEY_6) { // Reset
    editMode=EM_CONFIRM; confirmAction=0;
    renderConfirm("Reset all?");
    return;
  }
  if (key == KEY_7) { // Enter Hack Menu
    inHack = true; hackMenu = HACK_MAIN; hackSel = 0; renderHack(); return;
  }
  if (key == KEY_8) { // Dump (简化为显示更多)
    pageRow = (pageRow+8)%16;
  }
  if (key == KEY_9) { // Exit
    editMode=EM_CONFIRM; confirmAction=1;
    renderConfirm("Exit Shell?");
    return;
  }
  if (key == KEY_ENTER) { // Enter = same as key 1 (edit)
    key = KEY_1;
    if (pi.ro) { playTone(200,200); return; }
    editVal = safeRAM[cursorAddr]; editNibble=0; editMode=EM_EDIT;
    renderEdit(); return;
  }

  renderDump();
  // 更新 OLED 信息
  oledClear();
  snprintf((char*)inputBuf,32,"ADR:0x%02X",cursorAddr);
  oledDrawString(0,0,(char*)inputBuf);
  char vstr[16]; fmtVal(vstr, safeRAM[cursorAddr]);
  snprintf((char*)inputBuf,32,"VAL:%s",vstr);
  oledDrawString(0,15,(char*)inputBuf);
  if (pi.name) { oledDrawString(0,35,pi.name); }
  oledDrawString(0,52,"9:exit");
  oledRefresh();
}
