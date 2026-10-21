#pragma once

#include <QList>
#include <QString>

// 识别出的一个"词"（whisper 的 token 拼成完整 UTF-8 后的最小单位），时间单位：秒
struct Word {
    double start = 0;
    double end = 0;
    QString text;
};

// 一条字幕。翻译只填 zh，不改时间轴
struct Segment {
    int id = -1;          // 处理过程中的稳定编号（分段识别时行不一定按时间顺序产生）
    double start = 0;
    double end = 0;
    QString ja;           // 日文原文
    QString zh;           // 中文译文；空 = 还没翻译或翻译失败
    QString jaOriginal;   // 校对改过日文时保留语音识别的原文；空 = 没改过
    QString zhOriginal;   // 校对改过译文时保留原来的译文；空 = 没改过
    QList<Word> words;    // 词级时间戳，断句用；导出时不需要

    double duration() const { return end - start; }
};

// 由词重新拼出原文：日文不留空格，英文单词之间保留一个空格
QString joinWords(const QList<Word>& words);

// 字幕"长度"：不计空白的字符数
int visibleLength(const QString& text);

// 去掉识别"卡住"时反复输出的同一句话：连续 3 次以上、不少于 6 个字的相同文字只保留前 2 次
// （"XG!" 这类短口号连喊是真的，不处理）
QList<Segment> dropRepeats(const QList<Segment>& raw);
