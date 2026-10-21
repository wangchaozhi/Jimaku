#include "check.h"
#include "core/segmenter.h"

// 按给定的词和间隔拼一个 whisper 原始分段：每个词时长 dur，词间停顿 gap
static Segment rawSegment(const QStringList& words, double start, double dur = 0.3, double gap = 0.0)
{
    Segment s;
    double t = start;
    for (const QString& w : words) {
        s.words.append({t, t + dur, w});
        t += dur + gap;
    }
    s.start = s.words.first().start;
    s.end = s.words.last().end;
    s.ja = joinWords(s.words);
    return s;
}

static bool monotonic(const QList<Segment>& lines)
{
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].end <= lines[i].start)
            return false;
        if (i > 0 && lines[i].start < lines[i - 1].end)
            return false;
    }
    return true;
}

int main()
{
    SegmenterOptions o;

    // 短句原样保留
    {
        const auto lines = resegment({rawSegment({QStringLiteral("こんにちは"), QStringLiteral("。")}, 1.0)}, o);
        expectEq("short sentence kept", int(lines.size()), 1);
        expectEq("short sentence text", lines.first().ja, QStringLiteral("こんにちは。"));
    }

    // 句号处切开
    {
        const auto lines = resegment({rawSegment({QStringLiteral("今日は"), QStringLiteral("晴れ。"), QStringLiteral("明日は"),
                                                   QStringLiteral("雨です。")}, 0.0, 0.6)}, o);
        expectEq("split at sentence end", int(lines.size()), 2);
        expectEq("first sentence", lines.value(0).ja, QStringLiteral("今日は晴れ。"));
    }

    // 超过 18 字时在逗号处切，而不是硬切
    {
        const auto lines = resegment({rawSegment({QStringLiteral("メンバー全員が"), QStringLiteral("日本人の、"), QStringLiteral("7人組"),
                                                   QStringLiteral("ガールズグループ")}, 0.0, 0.5)}, o);
        check("long line split", lines.size() == 2, QString::number(lines.size()));
        expectEq("split after comma", lines.value(0).ja, QStringLiteral("メンバー全員が日本人の、"));
        bool allShort = true;
        for (const Segment& s : lines)
            allShort = allShort && visibleLength(s.ja) <= o.maxChars;
        check("every line within max chars", allShort);
    }

    // 超过 6 秒时切开
    {
        QStringList words;
        for (int i = 0; i < 8; ++i)
            words << QStringLiteral("あ");
        const auto lines = resegment({rawSegment(words, 0.0, 1.0)}, o);
        bool ok = lines.size() >= 2;
        for (const Segment& s : lines)
            ok = ok && s.duration() <= o.maxDuration + 1e-6;
        check("long duration split", ok, QString::number(lines.size()));
    }

    // 长停顿处换行
    {
        const auto lines = resegment({rawSegment({QStringLiteral("えっと"), QStringLiteral("それでは")}, 0.0, 0.8, 1.5)}, o);
        expectEq("split at long pause", int(lines.size()), 2);
    }

    // 片假名词被 token 拆开时不在中间断
    {
        const auto lines = resegment({rawSegment({QStringLiteral("全米の"), QStringLiteral("ビル"), QStringLiteral("ボード"),
                                                   QStringLiteral("チャートで"), QStringLiteral("1位を獲得しました")}, 0.0, 0.5)}, o);
        bool intact = true;
        for (const Segment& s : lines)
            intact = intact && !s.ja.startsWith(QStringLiteral("ボード"));
        check("katakana word not broken", intact);
    }

    // 停顿落在一个片假名词中间（token 时间不准）时也不拆开
    {
        Segment s = rawSegment({QStringLiteral("それは"), QStringLiteral("パフ")}, 0.0, 0.4);
        s.words.append({2.0, 2.4, QStringLiteral("ォーマンス力")});
        const auto lines = resegment({s}, o);
        bool intact = true;
        for (const Segment& l : lines)
            intact = intact && !l.ja.startsWith(QChar(0x30A9));
        check("pause does not split katakana word", intact);
    }

    // 太短的碎片并到前一行
    {
        const auto lines = resegment({rawSegment({QStringLiteral("そうですね")}, 0.0, 0.5),
                                      rawSegment({QStringLiteral("はい")}, 0.8, 0.2)}, o);
        expectEq("tiny fragment merged", int(lines.size()), 1);
    }

    // 时间轴单调、不重叠
    {
        const auto lines = resegment({rawSegment({QStringLiteral("一つ目。"), QStringLiteral("二つ目。")}, 0.0, 0.1),
                                      rawSegment({QStringLiteral("三つ目。")}, 0.15, 0.1)}, o);
        check("timeline monotonic", monotonic(lines));
    }

    // 增量断句：逐段喂入，与一次性断句的文字一致，时间轴单调
    {
        const QList<Segment> raw = {
            rawSegment({QStringLiteral("今日は"), QStringLiteral("晴れ。")}, 0.0, 0.4),
            rawSegment({QStringLiteral("メンバー全員が"), QStringLiteral("日本人の、"), QStringLiteral("7人組"), QStringLiteral("ガールズグループ")}, 2.0, 0.5),
            rawSegment({QStringLiteral("XG")}, 4.1, 0.3),
            rawSegment({QStringLiteral("そうですね")}, 6.0, 0.5),
            rawSegment({QStringLiteral("はい")}, 6.8, 0.2),
        };
        IncrementalSegmenter inc;
        QList<Segment> got;
        for (const Segment& r : raw)
            got += inc.feed({r});
        got += inc.finish();
        const QList<Segment> want = resegment(raw);
        QStringList a, b;
        for (const Segment& s : got)
            a << s.ja;
        for (const Segment& s : want)
            b << s.ja;
        expectEq("incremental matches batch", a.join(QLatin1Char('|')), b.join(QLatin1Char('|')));
        check("incremental timeline monotonic", monotonic(got));
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
