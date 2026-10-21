#include "segmentedit.h"

#include <algorithm>
#include <cmath>

namespace SegmentEdit {

namespace {

// 按比例切字符串（中日文按字切，不处理英文单词边界：拆分后用户通常会再改）
QPair<QString, QString> cut(const QString& text, double ratio)
{
    const int n = int(text.size());
    const int k = qBound(n > 1 ? 1 : 0, int(std::lround(n * ratio)), qMax(0, n - 1));
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
    int k = 0;
    while (k < s.words.size() && s.words[k].end <= at + 1e-6)
        ++k;
    if (k > 0 && k < s.words.size()) {
        first->words = s.words.mid(0, k);
        second->words = s.words.mid(k);
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
