#include "check.h"
#include "core/segmenter.h"

#include <cmath>

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

    // 连说的同一个词并成一行；间隔太大或合并后太长就不并
    {
        QList<Segment> in;
        for (int i = 0; i < 5; ++i)
            in.append(rawSegment({QStringLiteral("うん")}, i * 1.0));
        in.append(rawSegment({QStringLiteral("うん")}, 10.0));
        const auto merged = mergeRepeatedLines(in);
        expectEq("repeats merged", int(merged.size()), 2);
        expectEq("merged end", merged.first().end, in[4].end);
        expectEq("merged words", int(merged.first().words.size()), 5);
        QList<Segment> longRun;
        for (int i = 0; i < 10; ++i)
            longRun.append(rawSegment({QStringLiteral("あー")}, i * 1.0, 0.8));
        for (const Segment& s : mergeRepeatedLines(longRun))
            expectEq("merged line within max duration", s.end - s.start <= 6.0, true);
    }

    // 同时开始的两行：后一行往后挪，前一行至少显示 0.5 秒
    {
        QList<Segment> lines(2);
        lines[0].start = 10.0;
        lines[0].end = 10.5;
        lines[1].start = 10.0;
        lines[1].end = 11.0;
        fixTiming(&lines);
        expectEq("overlap: second pushed after first", lines[1].start, 10.5);
        expectEq("overlap: second keeps its end", lines[1].end, 11.0);
        lines[0].end = 10.1; // 第一行很短
        lines[1].start = 10.0;
        lines[1].end = 10.2;
        fixTiming(&lines);
        expectEq("short first line extended", lines[0].end, 10.5);
        expectEq("monotonic after fix", monotonic(lines), true);

        // 增量断句：后一批的开头不早于前一批已经交出去的结尾
        IncrementalSegmenter inc(o);
        const auto first = inc.feed({rawSegment({QStringLiteral("すごい"), QStringLiteral("!")}, 20.0, 0.2)});
        auto second = inc.feed({rawSegment({QStringLiteral("つんで"), QStringLiteral("!")}, 20.0, 0.4)});
        second += inc.finish();
        expectEq("incremental: first emitted", int(first.size()), 1);
        expectEq("incremental: no overlap", !second.isEmpty() && second.first().start >= first.last().end, true);
    }

    // 噪音过滤
    {
        auto line = [](const QStringList& words, const QList<double>& p, double confidence) {
            Segment s = rawSegment(words, 0.0);
            for (int i = 0; i < p.size(); ++i)
                s.words[i].p = p[i];
            s.confidence = confidence;
            return s;
        };
        Segment s = line({QStringLiteral("ぐぐぐ"), QStringLiteral("Lastly")}, {0.9, 0.1}, 0.43);
        expectEq("embedded garbage kept line", cleanNoise(&s), true);
        expectEq("embedded garbage removed", s.ja, QStringLiteral("ぐぐぐ"));
        s = line({QStringLiteral("にゅー"), QStringLiteral("じХUm")}, {0.9, 0.9}, 0.6);
        expectEq("cyrillic kept line", cleanNoise(&s), true);
        expectEq("cyrillic removed", s.ja, QStringLiteral("にゅー"));
        s = line({QStringLiteral("XG"), QStringLiteral("の"), QStringLiteral("リーダー")}, {0.95, 0.9, 0.9}, 0.92);
        expectEq("confident latin kept line", cleanNoise(&s), true);
        expectEq("confident latin in japanese kept", s.ja, QStringLiteral("XGのリーダー"));
        s = line({QStringLiteral("X"), QStringLiteral("G"), QStringLiteral("の")}, {0.3, 0.99, 0.9}, 0.78);
        expectEq("split latin token judged together", cleanNoise(&s) ? s.ja : QString(), QStringLiteral("XGの"));
        s = line({QStringLiteral("I"), QStringLiteral("just"), QStringLiteral("learned")}, {0.8, 0.8, 0.8}, 0.8);
        expectEq("english lyrics kept", cleanNoise(&s), true);
        s = line({QStringLiteral("High"), QStringLiteral("level")}, {0.3, 0.3}, 0.3);
        expectEq("unsure english dropped", cleanNoise(&s), false);
        s = line({QStringLiteral("えー"), QStringLiteral("後計")}, {0.01, 0.01}, 0.01);
        expectEq("very unsure dropped", cleanNoise(&s), false);
        s = line({QStringLiteral("ご視聴ありがとうございました。")}, {0.9}, 0.9);
        expectEq("known hallucination dropped", cleanNoise(&s), false);
        s = line({QStringLiteral("звCold")}, {0.9}, 0.9);
        expectEq("only foreign dropped", cleanNoise(&s), false);
    }

    // 译文里还有假名就算没翻译
    {
        Segment s;
        s.ja = QStringLiteral("いいね");
        expectEq("empty needs translation", needsTranslation(s), true);
        s.zh = QStringLiteral("いいね");
        expectEq("kana copy needs translation", needsTranslation(s), true);
        s.zh = QStringLiteral("クルーミス笑");
        expectEq("half-translated needs translation", needsTranslation(s), true);
        s.zh = QStringLiteral("不错");
        expectEq("translated", !needsTranslation(s), true);
        s.ja = QStringLiteral("High level");
        s.zh = QStringLiteral("High level");
        expectEq("english kept as is", !needsTranslation(s), true);
        s.zh = QStringLiteral("好——");
        expectEq("long dash is not kana", !needsTranslation(s), true);
    }

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
        for (int i = 0; i < 4; ++i)
            words << QStringLiteral("これは") << QStringLiteral("それが");
        const auto lines = resegment({rawSegment(words, 0.0, 1.0)}, o);
        bool ok = lines.size() >= 2;
        for (const Segment& s : lines)
            ok = ok && s.duration() <= o.maxDuration + 1e-6;
        check("long duration split", ok, QString::number(lines.size()));
    }

    // 一串断不开的片假名稍微超过 18 字：整串留在一行，不从词中间切开（ス|テージ），后面的汉字另起一行
    {
        const auto lines = resegment({rawSegment({QStringLiteral("クライ"), QStringLiteral("バック"), QStringLiteral("シリーズ"),
                                                  QStringLiteral("ファイナル"), QStringLiteral("ス"), QStringLiteral("テ"),
                                                  QStringLiteral("ージ"), QStringLiteral("阪神"), QStringLiteral("対"),
                                                  QStringLiteral("横浜")},
                                                 0.0, 0.3)},
                                     o);
        expectEq("katakana run kept whole", lines.value(0).ja, QStringLiteral("クライバックシリーズファイナルステージ"));
        expectEq("next word on new line", lines.value(1).ja, QStringLiteral("阪神対横浜"));
    }

    // 再长就只能硬切，但长音符、小写假名不能落到行首
    {
        const QStringList words{QStringLiteral("インター"), QStringLiteral("ナショナル"), QStringLiteral("チャンピオン"),
                                QStringLiteral("シップ"),   QStringLiteral("ファイナル"), QStringLiteral("トーナ"),
                                QStringLiteral("メント"),   QStringLiteral("ステ"),       QStringLiteral("ージ")};
        const auto lines = resegment({rawSegment(words, 0.0, 0.2)}, o);
        bool ok = lines.size() >= 2;
        for (const Segment& s : lines)
            ok = ok && !s.ja.startsWith(QStringLiteral("ー")) && visibleLength(s.ja) <= o.maxChars + o.overflowChars;
        check("hard split keeps long vowel", ok, QString::number(lines.size()));
    }

    // 长停顿处换行
    {
        const auto lines = resegment({rawSegment({QStringLiteral("えっと、"), QStringLiteral("それでは")}, 0.0, 0.8, 1.5)}, o);
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

    // 只有标点的行去掉；单个超长的词保留结尾、截到 6 秒
    {
        const auto lines = resegment({rawSegment({QStringLiteral("。")}, 0.0, 0.3), rawSegment({QStringLiteral("はい。")}, 3.0, 0.3)}, o);
        expectEq("punctuation-only line dropped", int(lines.size()), 1);
        const auto longWord = resegment({rawSegment({QStringLiteral("ああ")}, 10.0, 9.0)}, o);
        check("single long word clipped", longWord.size() == 1 && longWord.first().duration() <= o.maxDuration + 1e-6
                                              && std::abs(longWord.first().end - 19.0) < 1e-6);
    }

    // 平假名的词不从中间断开：时间超了也不拆（恥ずか|しい），只截短显示时间
    {
        const auto lines = resegment({rawSegment({QStringLiteral("恥ずか"), QStringLiteral("しい。")}, 0.0, 3.5)}, o);
        expectEq("hiragana word kept together", int(lines.size()), 1);
        expectEq("hiragana word text", lines.value(0).ja, QStringLiteral("恥ずかしい。"));
        check("over-long line clipped", lines.value(0).duration() <= o.maxDuration + 1e-6);
        // 助词后面可以断
        const auto ok = resegment({rawSegment({QStringLiteral("私は"), QStringLiteral("きのう"), QStringLiteral("見ました。")}, 0.0, 0.6, 1.0)}, o);
        check("split after particle allowed", ok.size() >= 2 && ok.first().ja == QStringLiteral("私は"));
        // 片假名接平假名词尾不断（ハビー|です）
        const auto desu = resegment({rawSegment({QStringLiteral("ハビー"), QStringLiteral("です")}, 0.0, 0.6, 1.0)}, o);
        expectEq("katakana + desu kept", int(desu.size()), 1);
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
