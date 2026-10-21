#include "check.h"
#include "core/furigana.h"

#include <QCoreApplication>
#include <QFileInfo>

static QString bracket(const QString& surface, const QString& reading)
{
    return Furigana::toBracketText(Furigana::align(surface, reading));
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // 对齐（与 furigana-web 的例子一致）
    expectEq("okurigana", bracket(QStringLiteral("白い"), QStringLiteral("シロイ")), QStringLiteral("白(しろ)い"));
    expectEq("okurigana 2", bracket(QStringLiteral("握り"), QStringLiteral("ニギリ")), QStringLiteral("握(にぎ)り"));
    expectEq("jukujikun", bracket(QStringLiteral("明日"), QStringLiteral("アシタ")), QStringLiteral("明日(あした)"));
    expectEq("kana prefix", bracket(QStringLiteral("お茶"), QStringLiteral("オチャ")), QStringLiteral("お茶(ちゃ)"));
    expectEq("verb stem", bracket(QStringLiteral("走っ"), QStringLiteral("ハシッ")), QStringLiteral("走(はし)っ"));
    expectEq("long adjective", bracket(QStringLiteral("恥ずかしい"), QStringLiteral("ハズカシイ")), QStringLiteral("恥(は)ずかしい"));
    expectEq("kana only", bracket(QStringLiteral("です"), QStringLiteral("デス")), QStringLiteral("です"));
    expectEq("katakana word", bracket(QStringLiteral("ビルボード"), QStringLiteral("ビルボード")), QStringLiteral("ビルボード"));
    expectEq("no reading", bracket(QStringLiteral("漢字"), QString()), QStringLiteral("漢字"));
    expectEq("kana between kanji", bracket(QStringLiteral("取り扱い"), QStringLiteral("トリアツカイ")), QStringLiteral("取(と)り扱(あつか)い"));
    // 读音和写法对不上：多个汉字组时不注音，不编造
    expectEq("mismatch multi-kanji", bracket(QStringLiteral("取り扱い"), QStringLiteral("ナニカ")), QStringLiteral("取り扱い"));
    expectEq("katakana to hiragana", Furigana::katakanaToHiragana(QStringLiteral("ジュリン・ヴ")), QStringLiteral("じゅりん・ゔ"));

    // 有字典时测完整的分词 + 注音（CI 上没有字典就跳过）
    const QString dir = qEnvironmentVariable("JIMAKU_DICT_DIR", Furigana::dictionaryDir());
    QString error;
    if (QFileInfo::exists(dir + QStringLiteral("/sys.dic")) && FuriganaTagger::instance().load(dir, &error)) {
        const auto parts = FuriganaTagger::instance().annotate(QStringLiteral("白い犬が明日走った。"));
        expectEq("annotate sentence", Furigana::toBracketText(parts), QStringLiteral("白(しろ)い犬(いぬ)が明日(あした)走(はし)った。"));
        expectEq("annotate keeps spaces", Furigana::toBracketText(FuriganaTagger::instance().annotate(QStringLiteral("XG の 日向"))),
                 QStringLiteral("XG の 日向(ひなた)"));
    } else {
        std::printf("skip annotate (dictionary %s: %s)\n", qPrintable(dir), qPrintable(error.isEmpty() ? QStringLiteral("not found") : error));
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
