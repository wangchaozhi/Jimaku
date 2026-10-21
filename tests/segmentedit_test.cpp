#include "check.h"
#include "core/segmentedit.h"

#include <cmath>

using namespace SegmentEdit;

int main()
{
    Segment s;
    s.id = 7;
    s.start = 10;
    s.end = 14;
    s.words = {{10, 11, QStringLiteral("今日は")}, {11, 12, QStringLiteral("晴れ")}, {12, 13, QStringLiteral("明日は")},
               {13, 14, QStringLiteral("雨")}};
    s.ja = joinWords(s.words);
    s.zh = QStringLiteral("今天晴明天雨");

    // 按词拆分
    Segment a, b;
    check("split ok", split(s, 12.0, &a, &b));
    expectEq("split first ja", a.ja, QStringLiteral("今日は晴れ"));
    expectEq("split second ja", b.ja, QStringLiteral("明日は雨"));
    check("split times", a.end == 12.0 && b.start == 12.0 && a.start == 10 && b.end == 14);
    expectEq("split zh by text ratio", a.zh + QLatin1Char('|') + b.zh, QStringLiteral("今天晴|明天雨"));
    check("first keeps id, second new", a.id == 7 && b.id == -1);
    check("split near edge refused", !split(s, 10.1, &a, &b));

    // 没有词级时间：按时间比例切字符
    Segment plain = s;
    plain.words.clear();
    check("plain split", split(plain, 11.0, &a, &b) && !a.ja.isEmpty() && !b.ja.isEmpty());

    // 合并
    Segment x;
    x.start = 1;
    x.end = 2;
    x.ja = QStringLiteral("恥ずか");
    x.zh = QStringLiteral("好");
    Segment y;
    y.start = 2;
    y.end = 3;
    y.ja = QStringLiteral("しい");
    y.zh = QStringLiteral("害羞");
    const Segment m = merge(x, y);
    expectEq("merge ja", m.ja, QStringLiteral("恥ずかしい"));
    expectEq("merge zh", m.zh, QStringLiteral("好 害羞"));
    check("merge times", m.start == 1 && m.end == 3);

    // 时间不会颠倒
    Segment t = x;
    setStart(&t, 5);
    check("start clamped before end", t.start < t.end && std::abs(t.end - t.start - kMinDuration) < 1e-9);
    setEnd(&t, 0);
    check("end clamped after start", t.end > t.start);

    // 排序保留 id
    QList<Segment> list{y, x};
    list[0].id = 1;
    list[1].id = 2;
    sortByStart(&list);
    check("sorted by start, ids kept", list[0].id == 2 && list[1].id == 1);

    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
