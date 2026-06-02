#ifndef SAFE_RAM_H
#define SAFE_RAM_H
#include <Arduino.h>
#define SAFE_RAM_SIZE 64

// 地址映射 (enum 自动递增, 不重复)
enum SafeRamAddr {
  // ── 显示设置 (0x10-0x1F) ──
  SAFE_DISP_BG    = 0x10,  // R/W 背景色索引 0-6
  SAFE_DISP_FONT  = 0x11,  // R/W 字体大小 1-3

  // ── 扫雷 (0x20-0x2F) ──
  SAFE_GAME_TYPE  = 0x20,  // RO  0=扫雷
  SAFE_MS_WIDTH   = 0x21,  // R/W 8-16
  SAFE_MS_HEIGHT  = 0x22,  // R/W 8-16
  SAFE_MS_MINES   = 0x23,  // R/W 1 ~ w*h*0.4
  SAFE_MS_CHEAT   = 0x24,  // R/W 0/1
  SAFE_MS_STATE   = 0x25,  // RO  0=playing 1=won 2=lost

  // ── 钢琴块 (0x30-0x3F) ──
  SAFE_PN_SONG        = 0x30,  // R/W 当前曲目 0-5
  SAFE_PN_SCROLL      = 0x31,  // R/W 下落速度% 50-200
  SAFE_PN_PERFECT_MS  = 0x32,  // R/W Perfect窗口ms 10-200
  SAFE_PN_AUTOPLAY    = 0x33,  // R/W 自动弹奏 0/1
  SAFE_PN_NOMISS      = 0x34,  // R/W 无Miss 0/1
  SAFE_PN_BPM         = 0x35,  // R/W BPM 60-240
};

extern uint8_t safeRAM[SAFE_RAM_SIZE];
void safeRAMInit();
uint8_t safeRAMPeek(uint8_t addr);
void safeRAMPoke(uint8_t addr, uint8_t val);
#endif
