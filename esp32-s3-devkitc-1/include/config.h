#ifndef CONFIG_H
#define CONFIG_H

// ========== LCD ILI9341 (14脚模块, 带SD卡槽) SPI引脚 ==========
// 模块引脚: VCC GND CS RST DC SDI SCK LED SDO (显示) + 4xSD卡(未用)
#define LCD_CS_PIN    10
#define LCD_DC_PIN    4
#define LCD_RST_PIN   8
#define LCD_MOSI_PIN  11
#define LCD_MISO_PIN  13
#define LCD_SCLK_PIN  12

// ========== OLED (SSD1306) I2C引脚 ==========
#define OLED_SDA_PIN  6
#define OLED_SCL_PIN  7
#define OLED_ADDR     0x3C

// ========== 4x4矩阵键盘引脚 ==========
#define KB_ROW1  1
#define KB_ROW2  2
#define KB_ROW3  3
#define KB_ROW4  5
#define KB_COL1  14
#define KB_COL2  15
#define KB_COL3  16
#define KB_COL4  17

// ========== 无源蜂鸣器 (3脚: VCC/GND/IO, 低电平触发) ==========
// VCC→蜂鸣器→IO(18), IO=LOW时导通发声
#define AUDIO_PIN     18
#define AUDIO_CHANNEL 0

// ========== 键值定义（与 keyboard.cpp 中的 keyMap 对应）==========
#define KEY_NONE   0
#define KEY_UP     1
#define KEY_DOWN   2
#define KEY_LEFT   3
#define KEY_RIGHT  4
#define KEY_ENTER  5
#define KEY_BACK   6
#define KEY_FIRE   7
#define KEY_1      8
#define KEY_2      9
#define KEY_3      10
#define KEY_4      11
#define KEY_5      12
#define KEY_6      13
#define KEY_7      14
#define KEY_8      15
#define KEY_9      16   // 仅 Ghost Shell 内部引用

// ========== 游戏参数 (ILI9341: 240x320 竖屏) ==========
#define LCD_WIDTH   240
#define LCD_HEIGHT  320
#define OLED_WIDTH  128
#define OLED_HEIGHT 64

#define FRAME_RATE  60
#define FRAME_MS    (1000 / FRAME_RATE)

#endif