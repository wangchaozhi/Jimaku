#include "segmenter.h"

namespace {

bool endsSentence(const QString& text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return false;
    static const QString marks = QStringLiteral("。！？!?…♪");
    return marks.contains(t.back());
}

bool endsClause(const QString& text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return false;
    static const QString marks = QStringLiteral("、，,）)」』");
    return marks.contains(t.back());
}

// 以助词结尾的词后面是比较自然的换行点
bool endsParticle(const QString& text)
{
    static const QStringList particles = {
        QStringLiteral("は"), QStringLiteral("が"), QStringLiteral("を"), QStringLiteral("に"), QStringLiteral("で"),
        QStringLiteral("と"), QStringLiteral("も"), QStringLiteral("から"), QStringLiteral("けど"), QStringLiteral("て"),
    };
    const QString t = text.trimmed();
    for (const QString& p : particles) {
        if (t.endsWith(p))
            return true;
    }
    return false;
}

enum class Script { Kanji, Katakana, Latin, Other };

Script scriptOf(QChar c)
{
    const ushort u = c.unicode();
    if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF))
        return Script::Kanji;
    if ((u >= 0x30A0 && u <= 0x30FF) || u == 0x30FC)
        return Script::Katakana;
    if (u < 0x80 && c.isLetterOrNumber())
        return Script::Latin;
    return Script::Other;
}

// 两侧是同一种文字（片假名接片假名、汉字接汉字…）时多半是同一个词被 token 拆开了，不要在这里断
bool splitsAWord(const Word& left, const Word& right)
{
    const QString l = left.text.trimmed();
    const QString r = right.text.trimmed();
    if (l.isEmpty() || r.isEmpty())
        return false;
    const Script a = scriptOf(l.back());
    const Script b = scriptOf(r.front());
    if (r.front() == QChar(0x30FC)) // 长音符 ー 不能放行首
        return true;
    return a == b && a != Script::Other;
}

int lengthOf(const QList<Word>& words, int from, int to)
{
    int n = 0;
    for (int i = from; i < to; ++i)
        n += visibleLength(words[i].text);
    return n;
}

// 在 words 里找最合适的断点 j（前半段为 [0, j)），找不到返回 -1
int bestSplit(const QList<Word>& words, const SegmenterOptions& o)
{
    int best = -1;
    double bestScore = -1;
    const int total = lengthOf(words, 0, words.size());
    for (int j = 1; j < words.size(); ++j) {
        const int left = lengthOf(words, 0, j);
        if (left < 3 || left > o.maxChars)
            continue;
        const Word& prev = words[j - 1];
        const Word& next = words[j];
        if (splitsAWord(prev, next))
            continue;
        double score = 0;
        if (endsClause(prev.text))
            score += 2;
        if (endsParticle(prev.text))
            score += 0.6;
        score += qMin(1.0, qMax(0.0, next.start - prev.end) * 2);
        // 同样合适时取更均衡的切法
        score += 0.3 * (1.0 - qAbs(total - 2.0 * left) / qMax(1, total));
        if (score > bestScore) {
            bestScore = score;
            best = j;
        }
    }
    return best;
}

Segment makeLine(const QList<Word>& words)
{
    Segment s;
    s.words = words;
    s.start = words.first().start;
    s.end = qMax(words.last().end, s.start);
    s.ja = joinWords(words);
    return s;
}

bool fits(const Segment& a, const Segment& b, const SegmenterOptions& o)
{
    return visibleLength(a.ja) + visibleLength(b.ja) <= o.maxChars && b.end - a.start <= o.maxDuration
           && b.start - a.end <= o.pauseSplit;
}

Segment merged(const Segment& a, const Segment& b)
{
    return makeLine(a.words + b.words);
}

} // namespace

QList<Segment> resegment(const QList<Segment>& raw, const SegmenterOptions& o)
{
    QList<Segment> lines;
    auto flush = [&](QList<Word>& cur) {
        if (!cur.isEmpty())
            lines.append(makeLine(cur));
        cur.clear();
    };

    for (const Segment& seg : raw) {
        QList<Word> cur;
        for (const Word& w : seg.words) {
            if (!cur.isEmpty() && w.start - cur.last().end > o.pauseSplit && !splitsAWord(cur.last(), w))
                flush(cur);
            cur.append(w);
            // 超长就在当前行里找断点，可能要切多次
            while (cur.size() > 1
                   && (lengthOf(cur, 0, cur.size()) > o.maxChars || cur.last().end - cur.first().start > o.maxDuration)) {
                int j = bestSplit(cur, o);
                if (j < 0)
                    j = cur.size() - 1; // 实在没有好位置：最后这个词另起一行
                QList<Word> head = cur.mid(0, j);
                cur = cur.mid(j);
                flush(head);
            }
            if (endsSentence(w.text))
                flush(cur);
        }
        flush(cur);
    }

    // 太短的碎片并到相邻行（优先并到前一行）
    for (int i = 0; i < lines.size();) {
        const Segment& s = lines[i];
        const bool tiny = s.duration() < o.minDuration || visibleLength(s.ja) <= 2;
        if (tiny && i > 0 && !endsSentence(lines[i - 1].ja) && fits(lines[i - 1], s, o)) {
            lines[i - 1] = merged(lines[i - 1], s);
            lines.removeAt(i);
            continue;
        }
        if (tiny && i + 1 < lines.size() && !endsSentence(s.ja) && fits(s, lines[i + 1], o)) {
            lines[i] = merged(s, lines[i + 1]);
            lines.removeAt(i + 1);
            continue;
        }
        ++i;
    }

    // 时间轴单调、每行至少显示 0.5 秒（不压到下一行）
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0 && lines[i].start < lines[i - 1].end)
            lines[i].start = lines[i - 1].end;
        if (lines[i].end - lines[i].start < 0.5) {
            const double limit = i + 1 < lines.size() ? lines[i + 1].start : lines[i].start + 0.5;
            lines[i].end = qMax(lines[i].end, qMin(lines[i].start + 0.5, limit));
        }
    }
    return lines;
}
