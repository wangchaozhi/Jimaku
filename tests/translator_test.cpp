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
    expectEq("user payload lines", user.value(QStringLiteral("lines")).toArray().at(0).toObject().value(QStringLiteral("ja")).toString(),
             QStringLiteral("こんにちは"));
    expectEq("user payload context", user.value(QStringLiteral("before")).toArray().at(0).toString(), QStringLiteral("前"));

    // 同一句话的几行编同一个句号
    const QList<int> groups = Translator::sentenceGroups({QStringLiteral("なぜ世界から"), QStringLiteral("注目を"), QStringLiteral("浴びているのか?"),
                                                          QStringLiteral("それは"), QStringLiteral("パフォーマンス力!")});
    expectEq("sentence groups", QStringLiteral("%1%2%3%4%5").arg(groups[0]).arg(groups[1]).arg(groups[2]).arg(groups[3]).arg(groups[4]),
             QStringLiteral("00011"));
    const QJsonObject grouped = QJsonDocument::fromJson(
        tr.buildMessages({QStringLiteral("なぜ"), QStringLiteral("世界から?"), QStringLiteral("XG")}, {}, {}).at(1).toObject()
            .value(QStringLiteral("content")).toString().toUtf8()).object();
    expectEq("payload sentence id", grouped.value(QStringLiteral("lines")).toArray().at(2).toObject().value(QStringLiteral("s")).toInt(), 1);

    // 字幕风格
    expectEq("style trailing period", Translator::applySubtitleStyle(QStringLiteral("全体成员都是日本人。")), QStringLiteral("全体成员都是日本人"));
    expectEq("style inner comma", Translator::applySubtitleStyle(QStringLiteral("12月，作为唯一的日本艺人，")), QStringLiteral("12月 作为唯一的日本艺人"));
    expectEq("style keeps question", Translator::applySubtitleStyle(QStringLiteral("为什么呢？")), QStringLiteral("为什么呢？"));
    expectEq("style keeps ellipsis", Translator::applySubtitleStyle(QStringLiteral("如今……")), QStringLiteral("如今……"));

    // 歌词选项写进提示词
    TranslatorConfig lyricsCfg;
    lyricsCfg.translateLyrics = true;
    check("prompt keeps lyrics by default", system.contains(QStringLiteral("保留原文")));
    check("prompt translates lyrics when asked",
          Translator(lyricsCfg).buildMessages({QStringLiteral("a")}, {}, {}).at(0).toObject().value(QStringLiteral("content")).toString()
              .contains(QStringLiteral("也翻译成中文")));

    // 校对回复解析：越界、空修改被丢弃
    const auto review = Translator::parseReview(QStringLiteral(
        "```json\n{\"summary\":\"XG 采访\",\"terms\":[{\"ja\":\"チサ\",\"zh\":\"CHISA\"},{\"ja\":\"\",\"zh\":\"x\"}],"
        "\"fixes\":[{\"i\":11,\"ja\":\"VOGUE\"},{\"i\":12,\"zh\":\"巡演\"},{\"i\":99,\"zh\":\"越界\"},{\"i\":13}]}\n```"), 10, 5);
    check("review parsed", review.ok);
    expectEq("review summary", review.summary, QStringLiteral("XG 采访"));
    expectEq("review terms", int(review.terms.size()), 1);
    expectEq("review fixes", int(review.fixes.size()), 2);
    expectEq("review fix ja", review.fixes.value(0).ja, QStringLiteral("VOGUE"));
    check("review garbage", !Translator::parseReview(QStringLiteral("无法校对"), 0, 5).ok);

    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
