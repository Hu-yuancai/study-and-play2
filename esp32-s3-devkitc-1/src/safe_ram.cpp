#include "safe_ram.h"
uint8_t safeRAM[SAFE_RAM_SIZE];

void safeRAMInit() {
  memset(safeRAM, 0, SAFE_RAM_SIZE);
  // 显示默认
  safeRAM[SAFE_DISP_BG]   = 0;   // 背景色索引0=深蓝黑
  safeRAM[SAFE_DISP_FONT] = 1;   // 字体1x
  // 扫雷默认
  safeRAM[SAFE_GAME_TYPE] = 0;
  safeRAM[SAFE_MS_WIDTH]  = 12;
  safeRAM[SAFE_MS_HEIGHT] = 12;
  safeRAM[SAFE_MS_MINES]  = 20;
  safeRAM[SAFE_MS_CHEAT]  = 0;
  safeRAM[SAFE_MS_STATE]  = 0;
  // 钢琴默认
  safeRAM[SAFE_PN_SONG]       = 0;
  safeRAM[SAFE_PN_SCROLL]     = 100;
  safeRAM[SAFE_PN_PERFECT_MS] = 72;
  safeRAM[SAFE_PN_AUTOPLAY]   = 0;
  safeRAM[SAFE_PN_NOMISS]     = 0;
  safeRAM[SAFE_PN_BPM]        = 120;
}

uint8_t safeRAMPeek(uint8_t addr) {
  return (addr < SAFE_RAM_SIZE) ? safeRAM[addr] : 0;
}

void safeRAMPoke(uint8_t addr, uint8_t val) {
  if (addr >= SAFE_RAM_SIZE) return;

  // 边界保护
  switch (addr) {
    case SAFE_DISP_BG:   if (val > 6) val = 6; break;
    case SAFE_DISP_FONT: if (val < 1) val = 1; else if (val > 3) val = 3; break;
    case SAFE_MS_WIDTH:  if (val < 8) val = 8; else if (val > 16) val = 16; break;
    case SAFE_MS_HEIGHT: if (val < 8) val = 8; else if (val > 16) val = 16; break;
    case SAFE_MS_MINES: {
      uint8_t mx = safeRAM[SAFE_MS_WIDTH] * safeRAM[SAFE_MS_HEIGHT] / 2;
      if (val < 1) val = 1; else if (val > mx) val = mx;
      break;
    }
    case SAFE_MS_CHEAT:  if (val > 1) val = 1; break;
    case SAFE_GAME_TYPE: case SAFE_MS_STATE: return; // RO
    case SAFE_PN_SONG:       if (val > 5) val = 5; break;
    case SAFE_PN_SCROLL:     if (val < 50) val = 50; else if (val > 200) val = 200; break;
    case SAFE_PN_PERFECT_MS: if (val < 10) val = 10; else if (val > 200) val = 200; break;
    case SAFE_PN_AUTOPLAY: case SAFE_PN_NOMISS: if (val > 1) val = 1; break;
    case SAFE_PN_BPM:        if (val < 60) val = 60; else if (val > 240) val = 240; break;
  }
  safeRAM[addr] = val;
}
