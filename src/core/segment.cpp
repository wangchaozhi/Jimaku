#include "segment.h"

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
