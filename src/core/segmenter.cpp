#include "segmenter.h"

#include <algorithm>

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

enum class Script { Kanji, Hiragana, Katakana, Latin, Other };

Script scriptOf(QChar c)
{
    const ushort u = c.unicode();
    if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF))
        return Script::Kanji;
    if (u >= 0x3041 && u <= 0x309F)
        return Script::Hiragana;
    if ((u >= 0x30A0 && u <= 0x30FF) || u == 0x30FC)
        return Script::Katakana;
    if (u < 0x80 && c.isLetterOrNumber())
        return Script::Latin;
    return Script::Other;
}

// 长音符、小写假名不能放行首
const QString kNoLineStart = QStringLiteral("ーぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ");

// 会不会把一个词从中间断开（whisper 的 token 不是词，常常把一个词拆成几段）。
// 日语字幕可以换行的地方：助词或标点之后，或者下一段以汉字、片假名、英文开头（新词开始）
bool splitsAWord(const Word& left, const Word& right)
{
    const QString l = left.text.trimmed();
    const QString r = right.text.trimmed();
    if (l.isEmpty() || r.isEmpty())
        return false;
    if (kNoLineStart.contains(r.front())) // 长音符、小写假名不能放行首
        return true;
    if (endsClause(l) || endsSentence(l))
        return false;
    const Script a = scriptOf(l.back());
    const Script b = scriptOf(r.front());
    if (b == Script::Hiragana) // 平假名开头的多半是词尾、活用或助词（恥ずか|しい、ハビー|です），只能接在助词后面
        return !endsParticle(l);
    return a == b && a != Script::Other; // 片假名接片假名、汉字接汉字、英文接英文
}

int lengthOf(const QList<Word>& words, int from, int to)
{
    int n = 0;
    for (int i = from; i < to; ++i)
        n += visibleLength(words[i].text);
    return n;
}

// 在 words 里找最合适的断点 j（前半段为 [0, j)，不超过 maxChars 个字），找不到返回 -1
int bestSplit(const QList<Word>& words, int maxChars)
{
    int best = -1;
    double bestScore = -1;
    const int total = lengthOf(words, 0, words.size());
    for (int j = 1; j < words.size(); ++j) {
        const int left = lengthOf(words, 0, j);
        if (left < 3 || left > maxChars)
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

// 放宽字数也找不到断点时的硬切：取不超长的最靠后的位置，但不让长音符、小写假名落到行首
// （ステ|ージ）；实在没有才切在最后一个词前
int hardSplit(const QList<Word>& words, const SegmenterOptions& o)
{
    for (int j = words.size() - 1; j >= 1; --j) {
        const QString r = words[j].text.trimmed();
        if (lengthOf(words, 0, j) <= o.maxChars && !r.isEmpty() && !kNoLineStart.contains(r.front()))
            return j;
    }
    return words.size() - 1;
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
                int j = bestSplit(cur, o.maxChars);
                if (j < 0) {
                    // 没有合适的断点：只是时间超了就不拆（后面会截短显示时间）
                    const int length = lengthOf(cur, 0, cur.size());
                    if (length <= o.maxChars)
                        break;
                    // 字数超了：先在稍长一点的范围里找不断词的断点（ファイナルステージ|阪神），
                    // 整行就是一串断不开的片假名时先留着，再长才硬切
                    const int softMax = o.maxChars + o.overflowChars;
                    j = bestSplit(cur, softMax);
                    if (j < 0) {
                        if (length <= softMax)
                            break;
                        j = hardSplit(cur, o);
                    }
                }
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

    // 紧挨着的相同字幕并成一行（连说的 うん、あー）
    lines = mergeRepeatedLines(lines, o.pauseSplit * 3, o.maxDuration);

    // 没有任何文字的行（静音处 whisper 常常只输出一个"。"）不是字幕
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                               [](const Segment& s) {
                                   return std::none_of(s.ja.cbegin(), s.ja.cend(), [](QChar c) { return c.isLetterOrNumber(); });
                               }),
                lines.end());
    // 超过最长时长又没法拆的行（多半是前面的静音算进了第一个词）：保留结尾，把开头往后挪
    for (Segment& s : lines) {
        if (s.duration() > o.maxDuration)
            s.start = s.end - o.maxDuration;
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

IncrementalSegmenter::IncrementalSegmenter(const SegmenterOptions& options)
    : m_options(options)
{
}

QList<Segment> IncrementalSegmenter::feed(const QList<Segment>& raw)
{
    if (raw.isEmpty())
        return {};
    // 留下的那一行当作一段原始分段放在最前面，和新来的一起重新断句
    QList<Segment> lines = resegment(m_pending + raw, m_options);
    m_pending.clear();
    // 最后一行以句末标点结尾时不会再和后面合并，可以直接定稿
    if (!lines.isEmpty() && !endsSentence(lines.last().ja))
        m_pending.append(lines.takeLast());
    return lines;
}

QList<Segment> IncrementalSegmenter::finish()
{
    QList<Segment> rest = m_pending;
    m_pending.clear();
    return rest;
}
