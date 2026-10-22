#include "segmentedit.h"

#include "segmenter.h"

#include <algorithm>
#include <cmath>

namespace SegmentEdit {

namespace {

enum class Kind { Hiragana, Katakana, Kanji, Latin, Mark };

Kind kindOf(QChar c)
{
    const ushort u = c.unicode();
    if (u >= 0x3041 && u <= 0x309F)
        return Kind::Hiragana;
    if ((u >= 0x30A0 && u <= 0x30FF) || (u >= 0xFF66 && u <= 0xFF9F))
        return Kind::Katakana;
    if ((u >= 0x3400 && u <= 0x9FFF) || u == 0x3005)
        return Kind::Kanji;
    return c.isLetterOrNumber() ? Kind::Latin : Kind::Mark;
}

// 在 k 处切开会不会把一个词断开：同一种文字连在一起（タイム|ズ）、或者把长音符、小写假名、标点放到后半段开头
bool badCut(const QString& text, int k)
{
    static const QString noStart = QStringLiteral("ーぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ、。，．,.!?！？…」』）)");
    if (noStart.contains(text[k]))
        return true;
    const Kind a = kindOf(text[k - 1]), b = kindOf(text[k]);
    return a == b && a != Kind::Mark && a != Kind::Hiragana;
}

// 按比例切字符串（没有词级时间时用）：在比例位置前后 3 个字内找不断词的地方（文字种类变化处、标点后）
QPair<QString, QString> cut(const QString& text, double ratio)
{
    const int n = int(text.size());
    int k = qBound(n > 1 ? 1 : 0, int(std::lround(n * ratio)), qMax(0, n - 1));
    if (k > 0 && k < n && badCut(text, k)) {
        for (int d = 1; d <= 3; ++d) {
            if (k - d >= 1 && !badCut(text, k - d)) {
                k -= d;
                break;
            }
            if (k + d <= n - 1 && !badCut(text, k + d)) {
                k += d;
                break;
            }
        }
    }
    return {text.left(k).trimmed(), text.mid(k).trimmed()};
}

} // namespace

bool split(const Segment& s, double at, Segment* first, Segment* second)
{
    if (at - s.start < kMinDuration || s.end - at < kMinDuration)
        return false;
    *first = s;
    *second = s;
    first->end = at;
    second->start = at;
    second->id = -1; // 新的一行，调用方分配 id
    first->jaOriginal.clear();
    second->jaOriginal.clear();
    first->zhOriginal.clear();
    second->zhOriginal.clear();

    const double ratio = (at - s.start) / (s.end - s.start);
    // 有词级时间：按词切原文，比例按切到的字数算（译文跟着这个比例切）
    // 词级时间要和现在的原文对得上（手工改过、校对改过原文的行不能再按旧的词切）
    const QList<Word> words = !s.words.isEmpty() && joinWords(s.words) == s.ja ? s.words : QList<Word>();
    int k = 0;
    while (k < words.size() && words[k].end <= at + 1e-6)
        ++k;
    // 播放位置常常落在一个词中间（ガ|ールズ）：改到 1 秒内最近的、不断词的地方，时间跟着词走
    if (breaksWord(words, k)) {
        int best = -1;
        double bestDistance = 1.0;
        for (int j = 1; j < words.size(); ++j) {
            const double t = words[j].start;
            if (breaksWord(words, j) || t - s.start < kMinDuration || s.end - t < kMinDuration)
                continue;
            if (std::abs(t - at) <= bestDistance) {
                bestDistance = std::abs(t - at);
                best = j;
            }
        }
        if (best > 0) {
            k = best;
            first->end = second->start = words[k].start;
        }
    }
    if (k > 0 && k < words.size()) {
        first->words = words.mid(0, k);
        second->words = words.mid(k);
        first->ja = joinWords(first->words);
        second->ja = joinWords(second->words);
        const double byText = double(visibleLength(first->ja)) / qMax(1, visibleLength(s.ja));
        const auto zh = cut(s.zh, byText);
        first->zh = zh.first;
        second->zh = zh.second;
    } else {
        first->words.clear();
        second->words.clear();
        const auto ja = cut(s.ja, ratio);
        const auto zh = cut(s.zh, ratio);
        first->ja = ja.first;
        second->ja = ja.second;
        first->zh = zh.first;
        second->zh = zh.second;
    }
    return true;
}

Segment merge(const Segment& a, const Segment& b)
{
    Segment m = a;
    m.start = qMin(a.start, b.start);
    m.end = qMax(a.end, b.end);
    m.ja = a.ja + b.ja;
    m.zh = (a.zh.isEmpty() || b.zh.isEmpty()) ? a.zh + b.zh : a.zh + QLatin1Char(' ') + b.zh;
    // 两行都有词级时间才拼起来（只有一半的话拆分时会对不上原文）
    if (!a.words.isEmpty() && !b.words.isEmpty())
        m.words = a.words + b.words;
    m.jaOriginal.clear();
    m.zhOriginal.clear();
    return m;
}

void setStart(Segment* s, double start)
{
    s->start = qMax(0.0, qMin(start, s->end - kMinDuration));
}

void setEnd(Segment* s, double end)
{
    s->end = qMax(end, s->start + kMinDuration);
}

void sortByStart(QList<Segment>* segments)
{
    std::stable_sort(segments->begin(), segments->end(), [](const Segment& a, const Segment& b) { return a.start < b.start; });
}

} // namespace SegmentEdit
