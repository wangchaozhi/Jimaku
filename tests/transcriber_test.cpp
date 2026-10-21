#include "check.h"
#include "core/transcriber.h"

int main()
{
    const QByteArray ka = QStringLiteral("か").toUtf8(); // 3 字节
    expectEq("complete", completeUtf8Length(ka), 3);
    expectEq("first byte only", completeUtf8Length(ka.left(1)), 0);
    expectEq("two of three", completeUtf8Length(ka.left(2)), 0);
    expectEq("ascii then partial", completeUtf8Length(QByteArray("ab") + ka.left(2)), 2);
    expectEq("two chars", completeUtf8Length(ka + ka), 6);
    expectEq("empty", completeUtf8Length(QByteArray()), 0);
    expectEq("emoji 4 bytes", completeUtf8Length(QStringLiteral("😀").toUtf8()), 4);

    QList<Word> words{{0, 1, QStringLiteral("XG")}, {1, 2, QStringLiteral("の")}, {2, 3, QStringLiteral(" Billboard")},
                      {3, 4, QStringLiteral(" chart")}};
    expectEq("join words", joinWords(words), QStringLiteral("XGのBillboard chart"));
    expectEq("join split ascii word", joinWords({{0, 1, QStringLiteral(" X")}, {1, 2, QStringLiteral("G")}, {2, 3, QStringLiteral("の")}}),
             QStringLiteral("XGの"));
    expectEq("join drops space before kana", joinWords({{0, 1, QStringLiteral("XG")}, {1, 2, QStringLiteral(" です")}}),
             QStringLiteral("XGです"));
    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
