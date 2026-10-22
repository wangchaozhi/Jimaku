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

namespace {

// 只留文字和数字：比较时忽略标点、空格、省略号
QString letters(const QString& text)
{
    QString out;
    for (QChar c : text) {
        if (c.isLetterOrNumber())
            out += c;
    }
    return out;
}

int editDistance(const QString& a, const QString& b)
{
    QList<int> row(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        row[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        int diag = row[0];
        row[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

// 长句允许差一两个字（"お前何言って" / "お前言って"），短句要完全相同
bool similar(const QString& a, const QString& b)
{
    if (a == b)
        return true;
    const int len = int(std::max(a.size(), b.size()));
    return len >= 6 && editDistance(a, b) * 5 <= len;
}

// 语气词、喘息（あー、うーん、はぁ、んっ）：连着出现很多次是真的，不当成卡住
bool isInterjection(const QString& text)
{
    static const QString chars = QStringLiteral("あいうえおぁぃぅぇぉんっゃゅょはひふへほアイウエオァィゥェォンッハヒフヘホー〜");
    return std::all_of(text.cbegin(), text.cend(), [](QChar c) { return chars.contains(c); });
}

} // namespace

QList<Segment> dropRepeats(const QList<Segment>& raw)
{
    QList<Segment> out;
    QString last;
    int run = 0;
    for (const Segment& s : raw) {
        const QString text = letters(s.ja);
        const bool same = !out.isEmpty() && !text.isEmpty() && similar(last, text);
        run = same ? run + 1 : 0;
        if (same) {
            if (text.size() >= 6 && run >= 2)
                continue; // 长句：只留前 2 次
            // 短句：连续 5 次以上才算卡住（ジャンク、あらめん），留前 4 次；语气词和英文口号（XG!）不管
            const bool chant = std::none_of(text.cbegin(), text.cend(), [](QChar c) { return c.unicode() >= 0x3000; });
            if (run >= 4 && !isInterjection(text) && !chant)
                continue;
        } else {
            last = text;
        }
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

namespace {

enum class Script { Japanese, Latin, Foreign, Neutral };

Script scriptOf(QChar c)
{
    const ushort u = c.unicode();
    if ((u >= 0x3041 && u <= 0x30FF) || (u >= 0x3400 && u <= 0x9FFF) || u == 0x3005 || (u >= 0xFF66 && u <= 0xFF9F)) // 含长音符 ー
        return Script::Japanese;
    if (!c.isLetter())
        return Script::Neutral;
    return u < 0x0250 || (u >= 0xFF21 && u <= 0xFF5A) ? Script::Latin : Script::Foreign;
}

bool hasScript(const QString& text, Script script)
{
    return std::any_of(text.cbegin(), text.cend(), [script](QChar c) { return scriptOf(c) == script; });
}

} // namespace

bool cleanNoise(Segment* s)
{
    static const QStringList hallucinations{
        QStringLiteral("ご視聴ありがとうございました"), QStringLiteral("ご覧いただきありがとうございます"),
        QStringLiteral("チャンネル登録"), QStringLiteral("最後までご視聴")};
    for (const QString& h : hallucinations) {
        if (s->ja.contains(h))
            return false;
    }
    if (s->confidence < 0.15)
        return false;
    const bool japanese = hasScript(s->ja, Script::Japanese);
    if (!japanese && s->confidence < 0.5)
        return false;

    // 连续的英文词按一整段判断（"XG" 会拆成 "X" "G" 两个 token，单看一个可能置信度很低）
    QList<Word> kept;
    for (int i = 0; i < s->words.size();) {
        const Word& w = s->words[i];
        if (hasScript(w.text, Script::Foreign)) {
            ++i;
            continue;
        }
        if (!japanese || !hasScript(w.text, Script::Latin)) {
            kept.append(w);
            ++i;
            continue;
        }
        int j = i;
        double sum = 0;
        while (j < s->words.size() && hasScript(s->words[j].text, Script::Latin)
               && !hasScript(s->words[j].text, Script::Foreign))
            sum += s->words[j++].p;
        if (sum / (j - i) >= 0.5)
            kept.append(s->words.mid(i, j - i));
        i = j;
    }
    if (kept.size() == s->words.size())
        return true;
    if (kept.isEmpty())
        return false;
    s->words = kept;
    s->ja = joinWords(kept);
    s->start = qMax(s->start, kept.first().start);
    s->end = qMax(s->start, qMin(s->end, kept.last().end));
    return hasScript(s->ja, Script::Japanese) || hasScript(s->ja, Script::Latin);
}

void fixTiming(QList<Segment>* lines, double after, double minDuration)
{
    double previousEnd = after;
    for (int i = 0; i < lines->size(); ++i) {
        Segment& s = (*lines)[i];
        s.start = qMax(s.start, previousEnd);
        if (s.end - s.start < minDuration) {
            double limit = i + 1 < lines->size() ? (*lines)[i + 1].start : s.start + minDuration;
            if (limit <= s.start) // 下一行和这一行同时开始：下一行会被往后挪
                limit = s.start + minDuration;
            s.end = qMax(s.end, qMin(s.start + minDuration, limit));
        }
        previousEnd = s.end;
    }
}

bool repeatsLoop(const QString& loop, const QString& text)
{
    const QString a = letters(loop), b = letters(text);
    return !b.isEmpty() && (a.contains(b) || b.contains(a) || similar(a, b));
}

bool onlyInterjection(const QString& text)
{
    const QString t = letters(text);
    return !t.isEmpty() && isInterjection(t);
}
