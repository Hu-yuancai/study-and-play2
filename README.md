   # ESP32-S3 学习游戏一体机

基于 ESP32-S3-DevKitC-1 的多功能嵌入式娱乐学习设备，集成钢琴游戏、飞机大战、学习伴侣三大模块。

> 微机接口原理 期末大作业

## 功能模块

| 模块 | 说明 |
|------|------|
| 🎹 钢琴游戏 | 音符下落 + 按键弹奏 + 连击计分 + 音频输出 |
| ✈️ 飞机大战 | 方向键控制 + 射击 + 多种敌机 + 等级递增 |
| 📚 学习伴侣 | 番茄钟计时(25/30/45/60min) + OLED表情动画 + 完成提醒 |

## 硬件清单

| 元器件 | 型号 | 接口 |
|--------|------|------|
| 主控板 | ESP32-S3-DevKitC-1 (N16R8) | USB-C |
| 主屏 | LCD12864 (ST7920) | SPI |
| 副屏 | OLED 0.96寸 (SSD1306) | I2C |
| 键盘 | 4×4 矩阵键盘 | GPIO |
| 功放 | LM386 模块 | PWM |
| 扬声器 | 8Ω 0.5W | 模拟 |

## 引脚分配

```
LCD12864:  CS=GPIO5, SCLK=GPIO18, MOSI=GPIO23
OLED:      SDA=GPIO21, SCL=GPIO22
键盘行:    GPIO32, GPIO33, GPIO25, GPIO26
键盘列:    GPIO27, GPIO14, GPIO12, GPIO13
音频PWM:   GPIO4 → LM386 IN+
```

## 项目结构

```
esp32_game_machine/
├── esp32_game_machine.ino   # 主程序 (菜单 + 模式切换)
├── config.h                 # 引脚定义与常量
├── game_common.h            # 共享类型定义
├── audio.h / .cpp           # PWM音频驱动
├── keyboard.h / .cpp        # 4x4矩阵键盘扫描
├── lcd_driver.h / .cpp      # LCD12864 SPI驱动
├── oled_driver.h / .cpp     # OLED I2C驱动
├── piano_game.h / .cpp      # 钢琴游戏
├── plane_game.h / .cpp      # 飞机大战
├── study_buddy.h / .cpp     # 学习伴侣
└── 项目文档.html             # 完整技术文档 (原理/接线/搭建教程)
```

## 开发环境

- **IDE**: Arduino IDE 2.x
- **开发板包**: ESP32 by Espressif (通过开发板管理器安装)
- **依赖库**: U8g2 (驱动LCD和OLED)
- **仿真**: [Wokwi](https://wokwi.com) (在线ESP32仿真)

### 快速开始

1. Arduino IDE → 文件 → 首选项 → 附加开发板管理器网址，添加：
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
2. 工具 → 开发板管理器 → 搜索 "esp32" → 安装
3. 工具 → 管理库 → 搜索 "U8g2" → 安装
4. 工具 → 开发板 → ESP32S3 Dev Module
5. 打开 `esp32_game_machine.ino`，点击上传

## 项目背景

本项目源于 [jan-bar/Temperature](https://github.com/jan-bar/Temperature) 的复现与扩展。原项目基于 STC89C51 实现温度采集、时钟显示与蜂鸣器音乐播放。我们将主控升级为 ESP32-S3，功能从单一温度监测扩展为集娱乐与学习于一体的多模块系统。

核心技术继承：
- 方波频率控制音调 (C51定时器中断 → ESP32 LEDC PWM)
- 模块化驱动设计 (显示/音频/输入分离)
- 矩阵键盘行列扫描算法

## 文档

打开 `项目文档.html` 查看完整技术文档，包含：
- 工作原理详解 (PWM音频、SPI/I2C通信、键盘扫描)
- Keil用户迁移指南 & Wokwi仿真教程
- 屏幕显示效果动态预览 (含OLED表情动画)
- 详细接线图 (ASCII图 + 引脚表)
- 面包板搭建步骤
- 单模块测试程序
- 团队分工指南

## 团队分工

| 成员 | 职责 | 核心文件 |
|------|------|----------|
| A | 硬件搭建 + 系统集成 | config.h, 实物接线 |
| B | 钢琴游戏开发 | piano_game.cpp |
| C | 飞机大战 + 音效 | plane_game.cpp, audio.cpp |
| D | 学习伴侣 + 表情 + 文档 | study_buddy.cpp, oled_driver.cpp |

## License

MIT
