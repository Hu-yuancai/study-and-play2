/*
 * audio.cpp - PWM音频驱动 (无源蜂鸣器, 低电平触发) — 非阻塞版本
 * 蜂鸣器3引脚: VCC / GND / IO(信号)
 * 电路: VCC→蜂鸣器→IO, 当IO=LOW时导通发声
 * 因此: 静音=IO HIGH, 发声=PWM 50%占空比
 *
 * 非阻塞原理: 音效被拆成一串 (频率,时长) 步骤存入序列, 立即返回;
 * 主循环每帧调用 audioUpdate() 按 millis() 推进到下一步, 不再 delay 卡住画面。
 */
#include <Arduino.h>
#include "audio.h"

#define DUTY_MAX 1023  // 10-bit PWM → 100% duty → IO HIGH → 静音

// ========== 非阻塞序列播放引擎 ==========
#define SFX_MAX_STEPS 16
static uint16_t seqFreq[SFX_MAX_STEPS];
static uint16_t seqDur[SFX_MAX_STEPS];
static int      seqLen = 0;
static int      seqIdx = 0;
static uint32_t stepEndTime = 0;
static bool     playing = false;

static void startStep(int i) {
    uint16_t f = seqFreq[i];
    if (f == 0) ledcWrite(AUDIO_CHANNEL, DUTY_MAX);   // 0 = 静音(休止)
    else        ledcWriteTone(AUDIO_CHANNEL, f);
    stepEndTime = millis() + seqDur[i];
}

static void startSequence(const uint16_t* freqs, const uint16_t* durs, int len) {
    if (len <= 0) return;
    if (len > SFX_MAX_STEPS) len = SFX_MAX_STEPS;
    for (int i = 0; i < len; i++) { seqFreq[i] = freqs[i]; seqDur[i] = durs[i]; }
    seqLen = len;
    seqIdx = 0;
    playing = true;
    startStep(0);
}

void initAudio() {
    ledcSetup(AUDIO_CHANNEL, 2000, 10);
    ledcAttachPin(AUDIO_PIN, AUDIO_CHANNEL);
    ledcWrite(AUDIO_CHANNEL, DUTY_MAX);  // HIGH = 低电平触发下静音
}

// 主循环每帧调用: 到时则推进到下一步, 序列结束后自动静音
void audioUpdate() {
    if (!playing) return;
    if ((int32_t)(millis() - stepEndTime) < 0) return;  // 当前步骤还没到时
    seqIdx++;
    if (seqIdx >= seqLen) {
        playing = false;
        ledcWrite(AUDIO_CHANNEL, DUTY_MAX);
        return;
    }
    startStep(seqIdx);
}

// 阻塞式单音 (仅开机提示音使用, 主循环尚未运行, 故保留阻塞)
void playTone(uint16_t frequency, uint16_t duration) {
    playing = false;  // 取消任何进行中的序列
    if (frequency == 0) {
        ledcWrite(AUDIO_CHANNEL, DUTY_MAX);
        delay(duration);
        return;
    }
    ledcWriteTone(AUDIO_CHANNEL, frequency);  // 50% duty 方波
    delay(duration);
    ledcWrite(AUDIO_CHANNEL, DUTY_MAX);
}

void stopTone() {
    playing = false;
    ledcWrite(AUDIO_CHANNEL, DUTY_MAX);
}

// 音高索引→频率表 (0=C4 .. 12=A5)
static const uint16_t kNoteFreqs[] = {
    NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4,
    NOTE_G4, NOTE_A4, NOTE_B4, NOTE_C5,
    NOTE_D5, NOTE_E5, NOTE_F5, NOTE_G5, NOTE_A5
};

void playNote(uint8_t noteIndex) {
    if (noteIndex < 13) {
        uint16_t f[1] = { kNoteFreqs[noteIndex] };
        uint16_t d[1] = { 80 };
        startSequence(f, d, 1);
    }
}

// 指定发声时长 (ms): 用于钢琴演奏, 让旋律音符按时值持续, 听起来更连贯
void playNoteDuration(uint8_t noteIndex, uint16_t ms) {
    if (noteIndex < 13) {
        if (ms < 40) ms = 40;
        uint16_t f[1] = { kNoteFreqs[noteIndex] };
        uint16_t d[1] = { ms };
        startSequence(f, d, 1);
    }
}

void playSFX_fire() {
    static const uint16_t f[] = { 1200, 800 };
    static const uint16_t d[] = {   30,  30 };
    startSequence(f, d, 2);
}

void playSFX_explode() {
    uint16_t f[SFX_MAX_STEPS], d[SFX_MAX_STEPS];
    int n = 0;
    for (int freq = 300; freq > 50 && n < SFX_MAX_STEPS; freq -= 30) {
        f[n] = freq; d[n] = 20; n++;
    }
    startSequence(f, d, n);
}

void playSFX_success() {
    static const uint16_t f[] = { NOTE_C5, NOTE_E5, NOTE_G5, (uint16_t)(NOTE_C5 * 2) };
    static const uint16_t d[] = {     150,     150,     150,                      150 };
    startSequence(f, d, 4);
}
