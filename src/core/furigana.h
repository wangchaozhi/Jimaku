#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
namespace MeCab {
class Tagger;
}

// 振假名：一段文字，ruby 为空表示不注音（假名、标点、英文）
struct RubyPart {
    QString text;
    QString ruby; // 平假名读音，只标在汉字上
};

namespace Furigana {

QString katakanaToHiragana(const QString& text);
bool containsKanji(const QString& text);

// 把一个词的读音对齐到写法上，只给汉字注音（移植自 furigana-web 的 aligner）：
// 白い/シロイ → 白(しろ)い；お茶/オチャ → お 茶(ちゃ)；明日/アシタ → 明日(あした)
QList<RubyPart> align(const QString& surface, const QString& reading);

// 表格里用的文字形式：白(しろ)い犬(いぬ)
QString toBracketText(const QList<RubyPart>& parts);

// IPADIC 字典（UTF-8，已编译）：首次使用时下载到 AppDataLocation/dict/ipadic
QString dictionaryDir();
bool dictionaryReady();

} // namespace Furigana

// 用 MeCab 分词、取读音，再逐词对齐。线程安全（内部加锁），结果按原文缓存
class FuriganaTagger {
public:
    static FuriganaTagger& instance();
    ~FuriganaTagger();

    bool load(const QString& dicDir, QString* error); // 已加载则什么都不做
    bool isLoaded() const;
    QList<RubyPart> annotate(const QString& text);

private:
    FuriganaTagger() = default;
    mutable QMutex m_mutex;
    std::unique_ptr<MeCab::Tagger> m_tagger;
    QHash<QString, QList<RubyPart>> m_cache;
};

// 下载并解压字典（约 13 MB），和识别模型的下载一样有进度、可取消
class DictionaryDownloader : public QObject {
    Q_OBJECT
public:
    explicit DictionaryDownloader(QObject* parent = nullptr);
    void start();
    void cancel();

signals:
    void progress(qint64 received, qint64 total);
    void finished(bool ok, const QString& error);

private:
    void extract(const QString& archive);

    QNetworkAccessManager* m_nam;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    bool m_canceled = false;
};
