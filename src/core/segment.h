#pragma once

#include <QList>
#include <QString>

// 识别出的一个"词"（whisper 的 token 拼成完整 UTF-8 后的最小单位），时间单位：秒
struct Word {
    double start = 0;
    double end = 0;
    QString text;
    double p = 1.0; // 识别置信度：组成这个词的 token 概率的平均值
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
    double confidence = 1.0; // 识别置信度（token 平均概率），只在识别时用来过滤噪音，不保存

    double duration() const { return end - start; }
};

// 由词重新拼出原文：日文不留空格，英文单词之间保留一个空格
QString joinWords(const QList<Word>& words);

// 字幕"长度"：不计空白的字符数
int visibleLength(const QString& text);

// 需要翻译：没有译文，或者译文里还有假名（模型把日语语气词原样返回了，或只翻了一半）
bool needsTranslation(const Segment& s);

// 合并紧挨着的相同字幕（うん、あー 连说十几次会变成十几行闪烁）：间隔不超过 maxGap 秒、
// 合并后不超过 maxDuration 秒的连续相同行并成一行
QList<Segment> mergeRepeatedLines(const QList<Segment>& lines, double maxGap = 1.5, double maxDuration = 6.0);

// 过滤识别噪音（呻吟、喘息、音乐被识别成乱码）。返回 false 表示整段丢掉，否则可能删掉其中的乱码词：
// - 置信度很低（< 0.15）的整段，以及 "ご視聴ありがとうございました" 这类 whisper 常见幻觉
// - 西里尔、韩文等日语视频里不会出现的文字
// - 日文行里夹着的低置信度（< 0.5）英文词；整行英文时置信度 < 0.5 才丢（真的英文歌词、口号一般 > 0.5）
bool cleanNoise(Segment* s);

// 去掉识别"卡住"时反复输出的同一句话（忽略标点比较）：
// - 不少于 6 个字的句子连续出现 3 次以上只保留前 2 次，差一两个字也算同一句
// - 短词连续 5 次以上只保留前 4 次；语气词（あー、うーん）和英文口号（"XG!"）连喊是真的，不处理
QList<Segment> dropRepeats(const QList<Segment>& raw);
