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
