#include "settings.h"

#include "modelmanager.h"

#include <QEventLoop>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <qtkeychain/keychain.h>

namespace Settings {

namespace {

const QString kService = QStringLiteral("Jimaku");

QString providerKey(const char* name)
{
    return QStringLiteral("translate/%1/%2").arg(providerId(), QLatin1String(name));
}

} // namespace

const QList<TranslateProvider>& providers()
{
    static const QList<TranslateProvider> list = {
        {QStringLiteral("deepseek"), QStringLiteral("DeepSeek"), QStringLiteral("https://api.deepseek.com/v1"), QStringLiteral("deepseek-chat"), true},
        {QStringLiteral("openai"), QStringLiteral("OpenAI"), QStringLiteral("https://api.openai.com/v1"), QStringLiteral("gpt-4o-mini"), true},
        {QStringLiteral("ollama"), QObject::tr("本地 Ollama（离线）"), QStringLiteral("http://127.0.0.1:11434/v1"), QStringLiteral("qwen2.5:7b"), false},
        {QStringLiteral("custom"), QObject::tr("其他 OpenAI 兼容服务"), QString(), QString(), true},
    };
    return list;
}

TranslateProvider provider(const QString& id)
{
    for (const TranslateProvider& p : providers()) {
        if (p.id == id)
            return p;
    }
    return providers().first();
}

QString providerId()
{
    return provider(QSettings().value(QStringLiteral("translate/provider")).toString()).id;
}

void setProviderId(const QString& id)
{
    QSettings().setValue(QStringLiteral("translate/provider"), id);
}

QString baseUrl()
{
    return QSettings().value(providerKey("baseUrl"), provider(providerId()).baseUrl).toString();
}

void setBaseUrl(const QString& url)
{
    QSettings().setValue(providerKey("baseUrl"), url.trimmed());
}

QString modelName()
{
    return QSettings().value(providerKey("model"), provider(providerId()).model).toString();
}

void setModelName(const QString& model)
{
    QSettings().setValue(providerKey("model"), model.trimmed());
}

QString asrModel()
{
    return ModelManager::find(QSettings().value(QStringLiteral("asr/model")).toString()).id;
}

void setAsrModel(const QString& id)
{
    QSettings().setValue(QStringLiteral("asr/model"), id);
}

SubtitleWriter::Format outputFormat()
{
    return QSettings().value(QStringLiteral("output/format")).toString() == QLatin1String("ass")
               ? SubtitleWriter::Format::Ass
               : SubtitleWriter::Format::Srt;
}

void setOutputFormat(SubtitleWriter::Format format)
{
    QSettings().setValue(QStringLiteral("output/format"),
                         format == SubtitleWriter::Format::Ass ? QStringLiteral("ass") : QStringLiteral("srt"));
}

SubtitleWriter::Layout layout()
{
    const QString v = QSettings().value(QStringLiteral("output/layout")).toString();
    if (v == QLatin1String("zh"))
        return SubtitleWriter::Layout::ChineseOnly;
    if (v == QLatin1String("ja"))
        return SubtitleWriter::Layout::JapaneseOnly;
    return SubtitleWriter::Layout::Bilingual;
}

void setLayout(SubtitleWriter::Layout layout)
{
    const char* v = layout == SubtitleWriter::Layout::ChineseOnly    ? "zh"
                    : layout == SubtitleWriter::Layout::JapaneseOnly ? "ja"
                                                                     : "both";
    QSettings().setValue(QStringLiteral("output/layout"), QLatin1String(v));
}

bool subtitleStyle()
{
    return QSettings().value(QStringLiteral("translate/subtitleStyle"), true).toBool();
}

void setSubtitleStyle(bool on)
{
    QSettings().setValue(QStringLiteral("translate/subtitleStyle"), on);
}

bool translateLyrics()
{
    return QSettings().value(QStringLiteral("translate/lyrics"), false).toBool();
}

void setTranslateLyrics(bool on)
{
    QSettings().setValue(QStringLiteral("translate/lyrics"), on);
}

bool reviewEnabled()
{
    return QSettings().value(QStringLiteral("translate/review"), true).toBool();
}

void setReviewEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("translate/review"), on);
}

QString glossaryText()
{
    return QSettings().value(QStringLiteral("translate/glossary")).toString();
}

void setGlossaryText(const QString& text)
{
    QSettings().setValue(QStringLiteral("translate/glossary"), text);
}

QList<QPair<QString, QString>> parseGlossary(const QString& text)
{
    QList<QPair<QString, QString>> out;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString ja = line.left(eq).trimmed();
        const QString zh = line.mid(eq + 1).trimmed();
        if (!ja.isEmpty() && !zh.isEmpty())
            out.append({ja, zh});
    }
    return out;
}

QString apiKey(const QString& providerId)
{
    const QString env = qEnvironmentVariable("JIMAKU_API_KEY");
    if (!env.isEmpty())
        return env;
    QKeychain::ReadPasswordJob job(kService);
    job.setAutoDelete(false);
    job.setKey(providerId);
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
    return job.error() == QKeychain::NoError ? job.textData() : QString();
}

bool setApiKey(const QString& providerId, const QString& key, QString* error)
{
    QEventLoop loop;
    QKeychain::Job* job = nullptr;
    QKeychain::WritePasswordJob write(kService);
    QKeychain::DeletePasswordJob remove(kService);
    if (key.isEmpty()) {
        job = &remove;
    } else {
        write.setTextData(key);
        job = &write;
    }
    job->setAutoDelete(false);
    job->setKey(providerId);
    QObject::connect(job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job->start();
    loop.exec();
    if (job->error() != QKeychain::NoError && job->error() != QKeychain::EntryNotFound) {
        *error = job->errorString();
        return false;
    }
    return true;
}

bool hasApiKey(const QString& providerId)
{
    return !apiKey(providerId).isEmpty();
}

TranslatorConfig translatorConfig()
{
    TranslatorConfig c;
    c.baseUrl = baseUrl();
    c.model = modelName();
    c.apiKey = apiKey(providerId());
    c.glossary = parseGlossary(glossaryText());
    c.subtitleStyle = subtitleStyle();
    c.translateLyrics = translateLyrics();
    c.cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/translate");
    return c;
}

} // namespace Settings
