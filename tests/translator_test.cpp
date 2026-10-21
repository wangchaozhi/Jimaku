#include "check.h"
#include "core/translator.h"

#include <QJsonDocument>
#include <QJsonObject>

int main()
{
    expectEq("plain object", Translator::parseReply(QStringLiteral("{\"t\":[\"你好\",\"再见\"]}"), 2).join(QLatin1Char('|')),
             QStringLiteral("你好|再见"));
    expectEq("fenced", Translator::parseReply(QStringLiteral("```json\n{\"t\": [\"一\"]}\n```"), 1).join(QLatin1Char('|')),
             QStringLiteral("一"));
    expectEq("bare array with chatter",
             Translator::parseReply(QStringLiteral("好的：[\"甲\", \"乙\"] 以上"), 2).join(QLatin1Char('|')),
             QStringLiteral("甲|乙"));
    expectEq("alt key", Translator::parseReply(QStringLiteral("{\"translations\":[\"a\"]}"), 1).join(QLatin1Char('|')),
             QStringLiteral("a"));
    check("count mismatch rejected", Translator::parseReply(QStringLiteral("{\"t\":[\"a\"]}"), 2).isEmpty());
    check("non-string rejected", Translator::parseReply(QStringLiteral("{\"t\":[1, 2]}"), 2).isEmpty());
    check("garbage rejected", Translator::parseReply(QStringLiteral("抱歉，我无法翻译"), 1).isEmpty());

    TranslatorConfig cfg;
    cfg.glossary = {{QStringLiteral("XG"), QStringLiteral("XG")}, {QStringLiteral("ジュリン"), QStringLiteral("JURIN")}};
    Translator tr(cfg);
    const QJsonArray msgs = tr.buildMessages({QStringLiteral("こんにちは")}, {QStringLiteral("前")}, {});
    const QString system = msgs.at(0).toObject().value(QStringLiteral("content")).toString();
    check("prompt asks for simplified chinese", system.contains(QStringLiteral("简体中文")));
    check("prompt includes glossary", system.contains(QStringLiteral("ジュリン → JURIN")));
    const QJsonObject user = QJsonDocument::fromJson(msgs.at(1).toObject().value(QStringLiteral("content")).toString().toUtf8()).object();
    expectEq("user payload lines", user.value(QStringLiteral("lines")).toArray().at(0).toString(), QStringLiteral("こんにちは"));
    expectEq("user payload context", user.value(QStringLiteral("before")).toArray().at(0).toString(), QStringLiteral("前"));

    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
