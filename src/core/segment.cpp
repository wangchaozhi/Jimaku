#include "segment.h"

#include <algorithm>

QString joinWords(const QList<Word>& words)
{
    // whisper 的英文 token 自带前导空格（" chart"），同一个单词拆开的 token 没有（"X" "G"）。
    // 所以按原样拼接，只保留两侧都是 ASCII 的空格（英文单词之间），日文旁边的空格去掉
    QString raw;
    for (const Word& w : words)
        raw += w.text;
    QString out;
    for (int i = 0; i < raw.size(); ++i) {
        if (!raw[i].isSpace()) {
            out += raw[i];
            continue;
        }
        int j = i;
        while (j < raw.size() && raw[j].isSpace())
            ++j;
        if (!out.isEmpty() && j < raw.size() && out.back().unicode() < 0x80 && raw[j].unicode() < 0x80)
            out += QLatin1Char(' ');
        i = j - 1;
    }
    return out;
}

int visibleLength(const QString& text)
{
    int n = 0;
    for (QChar c : text) {
        if (!c.isSpace())
            ++n;
    }
    return n;
}

QList<Segment> dropRepeats(const QList<Segment>& raw)
{
    QList<Segment> out;
    int run = 0;
    for (const Segment& s : raw) {
        const bool same = !out.isEmpty() && out.last().ja == s.ja;
        run = same ? run + 1 : 0;
        if (same && run >= 2 && visibleLength(s.ja) >= 6)
            continue;
        out.append(s);
    }
    return out;
}

bool needsTranslation(const Segment& s)
{
    const QString zh = s.zh.trimmed();
    if (zh.isEmpty())
        return true;
    // 译文里还有假名（长音符 ー 中文也用，不算）：原样返回或只翻了一半
    return std::any_of(zh.cbegin(), zh.cend(), [](QChar c) { return c.unicode() >= 0x3041 && c.unicode() <= 0x30FA; });
}

QList<Segment> mergeRepeatedLines(const QList<Segment>& lines, double maxGap, double maxDuration)
{
    QList<Segment> out;
    for (const Segment& s : lines) {
        if (!out.isEmpty()) {
            Segment& last = out.last();
            if (last.ja == s.ja && s.start - last.end <= maxGap && s.end - last.start <= maxDuration) {
                last.end = qMax(last.end, s.end);
                last.words += s.words;
                if (last.zh.isEmpty())
                    last.zh = s.zh;
                continue;
            }
        }
        out.append(s);
    }
    return out;
}
