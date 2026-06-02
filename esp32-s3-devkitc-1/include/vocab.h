/*
 * vocab.h - 背单词模块
 * 翻卡片学习 + 选择题测验, 内置常用词表
 */
#ifndef VOCAB_H
#define VOCAB_H

#include <Arduino.h>
#include "config.h"

void vocabInit();
void vocabLoop(uint8_t key);

#endif
