#include "check.h"
#include "core/subtitlewriter.h"

using namespace SubtitleWriter;

int main()
{
    expectEq("srt time", formatSrtTime(3723.456), QStringLiteral("01:02:03,456"));
    expectEq("srt time zero", formatSrtTime(0), QStringLiteral("00:00:00,000"));
    expectEq("ass time", formatAssTime(62.345), QStringLiteral("0:01:02.35"));

    Segment a;
    a.start = 1.0;
    a.end = 2.5;
    a.ja = QStringLiteral("こんにちは");
    a.zh = QStringLiteral("你好");
    Segment b;
    b.start = 3.0;
    b.end = 4.0;
    b.ja = QStringLiteral("XG!"); // 没有译文

    const QString both = toSrt({a, b}, Layout::Bilingual);
    expectEq("srt bilingual", both,
             QStringLiteral("1\n00:00:01,000 --> 00:00:02,500\n你好\nこんにちは\n\n2\n00:00:03,000 --> 00:00:04,000\nXG!\n\n"));
    check("srt chinese only falls back to ja", toSrt({b}, Layout::ChineseOnly).contains(QStringLiteral("XG!")));
    check("srt japanese only", !toSrt({a}, Layout::JapaneseOnly).contains(QStringLiteral("你好")));

    const QString ass = toAss({a}, Layout::Bilingual);
    check("ass dialogue", ass.contains(QStringLiteral("Dialogue: 0,0:00:01.00,0:00:02.50,Default,,0,0,0,,你好\\N{\\fs44}こんにちは")), ass);

    // 外观：顶部对齐（8）、字号按倍数
    const QString topAss = toAss({a}, Layout::Bilingual, Appearance{true, 1.5});
    check("ass top alignment and scaled size", topAss.contains(QStringLiteral("Style: Default,PingFang SC,96,"))
                                                   && topAss.contains(QStringLiteral(",1,3,1,8,60,60,50,1"))
                                                   && topAss.contains(QStringLiteral("{\\fs66}")));

    Segment braces = a;
    braces.zh = QStringLiteral("{危险}");
    check("ass escapes override braces", !toAss({braces}, Layout::ChineseOnly).contains(QStringLiteral("{危险}")));

    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
