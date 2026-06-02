/*
 * audio.h - 音频驱动 (PWM方波输出)
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>
#include "config.h"

// 标准音符频率定义 (C4 = 中音Do)
#define NOTE_C4  523
#define NOTE_D4  587
#define NOTE_E4  659
#define NOTE_F4  698
#define NOTE_G4  784
#define NOTE_A4  880
#define NOTE_B4  988
#define NOTE_C5  1047
#define NOTE_D5  1175
#define NOTE_E5  1319
#define NOTE_F5  1397
#define NOTE_G5  1568
#define NOTE_A5  1760
#define NOTE_B5  1976
#define NOTE_C3  262
#define NOTE_D3  294
#define NOTE_E3  330
#define NOTE_F3  349
#define NOTE_G3  392
#define NOTE_A3  440
#define NOTE_B3  494
#define NOTE_REST 0

void initAudio();
void audioUpdate();   // 在主循环中每帧调用, 推进非阻塞音效
void playTone(uint16_t frequency, uint16_t duration);
void stopTone();
void playNote(uint8_t noteIndex);
void playNoteDuration(uint8_t noteIndex, uint16_t ms);
void playSFX_fire();
void playSFX_explode();
void playSFX_success();

#endif
