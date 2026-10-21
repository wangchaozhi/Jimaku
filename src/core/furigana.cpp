#include "furigana.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <functional>
#include <map>
#include <mecab.h>
#include <optional>

namespace {

// PyPI 上的 ipadic 包：里面就是编译好的 UTF-8 字典（sys.dic 等），不用在本机编译
const char* kDictionaryUrl =
    "https://files.pythonhosted.org/packages/e7/4e/c459f94d62a0bef89f866857bc51b9105aff236b83928618315b41a26b7b/ipadic-1.0.0.tar.gz";

bool isKanji(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x3400 && u <= 0x4DBF) || (u >= 0x4E00 && u <= 0x9FFF) || (u >= 0xF900 && u <= 0xFAFF) || u == 0x3005 /*々*/
           || u == 0x3006 /*〆*/ || u == 0x30F5 /*ヵ*/ || u == 0x30F6 /*ヶ*/ || u == 0x3007 /*〇*/;
}

bool isKana(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x3040 && u <= 0x309F) || (u >= 0x30A0 && u <= 0x30FF) || (u >= 0x31F0 && u <= 0x31FF);
}

enum class Kind { Kanji, Kana, Other };

Kind kindOf(QChar c)
{
    if (isKanji(c))
        return Kind::Kanji;
    if (isKana(c))
        return Kind::Kana;
    return Kind::Other;
}

QString normalizeReading(const QString& text)
{
    return Furigana::katakanaToHiragana(text.normalized(QString::NormalizationForm_KC)).trimmed();
}

QList<RubyPart> mergePlain(const QList<RubyPart>& parts)
{
    QList<RubyPart> out;
    for (const RubyPart& p : parts) {
        if (p.text.isEmpty())
            continue;
        if (!out.isEmpty() && out.last().ruby.isEmpty() && p.ruby.isEmpty())
            out.last().text += p.text;
        else
            out.append(p);
    }
    return out;
}

} // namespace

namespace Furigana {

QString katakanaToHiragana(const QString& text)
{
    QString out = text;
    for (QChar& c : out) {
        if (c.unicode() >= 0x30A1 && c.unicode() <= 0x30F6)
            c = QChar(c.unicode() - 0x60);
    }
    return out;
}

bool containsKanji(const QString& text)
{
    return std::any_of(text.cbegin(), text.cend(), isKanji);
}

QList<RubyPart> align(const QString& surface, const QString& reading)
{
    if (surface.isEmpty())
        return {};
    if (!containsKanji(surface) || reading.isEmpty())
        return {{surface, QString()}};

    const QString r = normalizeReading(reading);
    // 按文字种类分组：汉字一组、假名一组、其他一组
    struct Group {
        Kind kind;
        QString text;
    };
    QList<Group> groups;
    for (QChar c : surface) {
        const Kind k = kindOf(c);
        if (!groups.isEmpty() && groups.last().kind == k)
            groups.last().text += c;
        else
            groups.append({k, QString(c)});
    }

    // 假名和其他字符是固定的锚点，汉字组在锚点之间分到至少一个读音字符（词很短，回溯就够快）
    std::map<std::pair<int, int>, std::optional<QList<RubyPart>>> memo;
    std::function<std::optional<QList<RubyPart>>(int, int)> solve = [&](int gi, int ri) -> std::optional<QList<RubyPart>> {
        if (gi == groups.size())
            return ri == r.size() ? std::optional<QList<RubyPart>>(QList<RubyPart>{}) : std::nullopt;
        const auto key = std::make_pair(gi, ri);
        if (auto it = memo.find(key); it != memo.end())
            return it->second;
        std::optional<QList<RubyPart>> result;
        const Group& g = groups[gi];
        if (g.kind != Kind::Kanji) {
            const QString literal = normalizeReading(g.text);
            if (r.mid(ri, literal.size()) == literal) {
                if (auto tail = solve(gi + 1, ri + int(literal.size()))) {
                    tail->prepend({g.text, QString()});
                    result = tail;
                }
            }
        } else {
            int minRemaining = 0;
            for (int j = gi + 1; j < groups.size(); ++j)
                minRemaining += groups[j].kind == Kind::Kanji ? 1 : int(normalizeReading(groups[j].text).size());
            for (int end = ri + 1; end <= r.size() - minRemaining && !result; ++end) {
                if (auto tail = solve(gi + 1, end)) {
                    tail->prepend({g.text, r.mid(ri, end - ri)});
                    result = tail;
                }
            }
        }
        memo[key] = result;
        return result;
    };

    if (const auto solved = solve(0, 0))
        return mergePlain(*solved);
    // 对不上（读音和写法不一致）：只有一个汉字组时整个读音给它，否则不注音，宁可不标也不标错
    int kanjiGroups = 0;
    for (const Group& g : groups)
        kanjiGroups += g.kind == Kind::Kanji ? 1 : 0;
    if (kanjiGroups != 1)
        return {{surface, QString()}};
    QList<RubyPart> out;
    for (const Group& g : groups)
        out.append({g.text, g.kind == Kind::Kanji ? r : QString()});
    return mergePlain(out);
}

QString toBracketText(const QList<RubyPart>& parts)
{
    QString out;
    for (const RubyPart& p : parts)
        out += p.ruby.isEmpty() ? p.text : p.text + QLatin1Char('(') + p.ruby + QLatin1Char(')');
    return out;
}

QString dictionaryDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/dict/ipadic");
}

bool dictionaryReady()
{
    return QFileInfo(dictionaryDir() + QStringLiteral("/sys.dic")).size() > 1024 * 1024;
}

} // namespace Furigana

FuriganaTagger& FuriganaTagger::instance()
{
    static FuriganaTagger tagger;
    return tagger;
}

FuriganaTagger::~FuriganaTagger() = default;

bool FuriganaTagger::isLoaded() const
{
    QMutexLocker lock(&m_mutex);
    return m_tagger != nullptr;
}

bool FuriganaTagger::load(const QString& dicDir, QString* error)
{
    QMutexLocker lock(&m_mutex);
    if (m_tagger)
        return true;
    // dicrc 本身就是合法的 mecabrc：用它，避免去找系统里的 mecabrc
    // 用 argv 形式传参：路径里有空格也没问题（字符串形式不认引号）
    // MeCab 在 Windows 上把路径当 UTF-8 转成宽字符；macOS 本来就是 UTF-8
    QByteArray rc = QDir(dicDir).filePath(QStringLiteral("dicrc")).toUtf8();
    QByteArray dir = dicDir.toUtf8();
    char prog[] = "mecab";
    char r[] = "-r";
    char d[] = "-d";
    char* argv[] = {prog, r, rc.data(), d, dir.data()};
    m_tagger.reset(MeCab::createTagger(5, argv));
    if (!m_tagger) {
        *error = QStringLiteral("MeCab: %1").arg(QString::fromUtf8(MeCab::getTaggerError()));
        return false;
    }
    return true;
}

QList<RubyPart> FuriganaTagger::annotate(const QString& text)
{
    QMutexLocker lock(&m_mutex);
    if (!m_tagger || text.isEmpty())
        return {{text, QString()}};
    if (auto it = m_cache.constFind(text); it != m_cache.cend())
        return *it;
    const QByteArray utf8 = text.toUtf8();
    QList<RubyPart> parts;
    for (const MeCab::Node* n = m_tagger->parseToNode(utf8.constData(), utf8.size()); n; n = n->next) {
        if (n->stat == MECAB_BOS_NODE || n->stat == MECAB_EOS_NODE)
            continue;
        // rlength 包含词前面的空白：保留原文里的空格
        const QString space = QString::fromUtf8(n->surface - (n->rlength - n->length), int(n->rlength - n->length));
        if (!space.isEmpty())
            parts.append({space, QString()});
        const QString surface = QString::fromUtf8(n->surface, int(n->length));
        const QStringList feature = QString::fromUtf8(n->feature).split(QLatin1Char(','));
        const QString reading = feature.size() > 7 && feature[7] != QLatin1String("*") ? feature[7] : QString();
        parts += Furigana::align(surface, reading);
    }
    parts = mergePlain(parts);
    if (m_cache.size() > 20000)
        m_cache.clear();
    m_cache.insert(text, parts);
    return parts;
}

DictionaryDownloader::DictionaryDownloader(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void DictionaryDownloader::start()
{
    m_canceled = false;
    const QString dir = QFileInfo(Furigana::dictionaryDir()).absolutePath();
    QDir().mkpath(dir);
    m_file.setFileName(dir + QStringLiteral("/ipadic.tar.gz.part"));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit finished(false, tr("无法写入 %1：%2").arg(m_file.fileName(), m_file.errorString()));
        return;
    }
    QNetworkRequest req(QUrl(QString::fromLatin1(kDictionaryUrl)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] { m_file.write(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::downloadProgress, this, &DictionaryDownloader::progress);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        m_file.write(reply->readAll());
        m_file.close();
        if (m_canceled || reply->error() != QNetworkReply::NoError) {
            m_file.remove();
            emit finished(false, m_canceled ? QString() : tr("字典下载失败：%1").arg(reply->errorString()));
            return;
        }
        extract(m_file.fileName());
    });
}

void DictionaryDownloader::extract(const QString& archive)
{
    // 用系统自带的 tar 解压（macOS、Windows 10 及以后都有），只取出 dicdir
    QTemporaryDir tmp;
    QProcess tar;
    tar.start(QStringLiteral("tar"), {QStringLiteral("-xzf"), archive, QStringLiteral("-C"), tmp.path()});
    tar.waitForFinished(120000);
    QFile::remove(archive);
    const QString dicdir = tmp.filePath(QStringLiteral("ipadic-1.0.0/ipadic/dicdir"));
    if (tar.exitCode() != 0 || !QFileInfo::exists(dicdir + QStringLiteral("/sys.dic"))) {
        emit finished(false, tr("字典解压失败：%1").arg(QString::fromUtf8(tar.readAllStandardError()).trimmed()));
        return;
    }
    const QString target = Furigana::dictionaryDir();
    QDir(target).removeRecursively();
    QDir().mkpath(target);
    for (const QFileInfo& f : QDir(dicdir).entryInfoList(QDir::Files)) {
        if (!QFile::copy(f.absoluteFilePath(), target + QLatin1Char('/') + f.fileName())) {
            emit finished(false, tr("无法保存字典文件：%1").arg(f.fileName()));
            return;
        }
    }
    emit finished(true, QString());
}

void DictionaryDownloader::cancel()
{
    if (m_reply) {
        m_canceled = true;
        m_reply->abort();
    }
}
