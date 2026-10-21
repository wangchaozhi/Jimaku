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
    double start = 0;
    double end = 0;
    QString ja;           // 日文原文
    QString zh;           // 中文译文；空 = 还没翻译或翻译失败
    QList<Word> words;    // 词级时间戳，断句用；导出时不需要

    double duration() const { return end - start; }
};

// 由词重新拼出原文：日文不留空格，英文单词之间保留一个空格
QString joinWords(const QList<Word>& words);

// 字幕"长度"：不计空白的字符数
int visibleLength(const QString& text);
