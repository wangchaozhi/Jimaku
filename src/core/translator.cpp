#include "translator.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QMutex>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QRegularExpression>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

namespace {

const char* kSystemPrompt =
    "你是专业的日译中字幕译者。用户发来一个 JSON：lines 是需要翻译的日文字幕（按播放顺序），"
    "before / after 是前后文，只用来理解语境，不要翻译。\n"
    "要求：\n"
    "1. 译成自然、口语化的简体中文（不要用繁体字），符合字幕阅读习惯，不加任何解释或注释。\n"
    "2. 逐条翻译，条数和顺序必须与 lines 完全一致，不得合并、拆分或遗漏。\n"
    "3. 原文来自语音识别，可能有同音错字，请结合上下文按正确的意思翻译；人名、团体名前后保持一致，外来语用通行译名。\n"
    "4. 喊声、口号、歌词片段或拟声词，译不出意思时保留原文。\n"
    "只输出 JSON：{\"t\": [\"第1条译文\", \"第2条译文\", ...]}";

QStringList toStringList(const QJsonArray& arr)
{
    QStringList out;
    for (const QJsonValue& v : arr) {
        if (!v.isString())
            return {};
        out << v.toString().trimmed();
    }
    return out;
}

} // namespace

Translator::Translator(TranslatorConfig config)
    : m_config(std::move(config))
{
    while (m_config.baseUrl.endsWith(QLatin1Char('/')))
        m_config.baseUrl.chop(1);
}

QStringList Translator::parseReply(const QString& content, int expected)
{
    QString text = content.trimmed();
    // 去掉 ```json ... ``` 外壳
    static const QRegularExpression fence(QStringLiteral("```(?:json)?\\s*([\\s\\S]*?)```"));
    const auto m = fence.match(text);
    if (m.hasMatch())
        text = m.captured(1).trimmed();

    QStringList result;
    const int objStart = text.indexOf(QLatin1Char('{'));
    const int arrStart = text.indexOf(QLatin1Char('['));
    if (objStart >= 0 && (arrStart < 0 || objStart < arrStart)) {
        const QJsonDocument doc = QJsonDocument::fromJson(text.mid(objStart, text.lastIndexOf(QLatin1Char('}')) - objStart + 1).toUtf8());
        const QJsonObject obj = doc.object();
        for (const char* key : {"t", "translations", "lines"}) {
            if (obj.value(QLatin1String(key)).isArray()) {
                result = toStringList(obj.value(QLatin1String(key)).toArray());
                break;
            }
        }
    } else if (arrStart >= 0) {
        const QJsonDocument doc = QJsonDocument::fromJson(text.mid(arrStart, text.lastIndexOf(QLatin1Char(']')) - arrStart + 1).toUtf8());
        result = toStringList(doc.array());
    }
    return result.size() == expected ? result : QStringList();
}

QJsonArray Translator::buildMessages(const QStringList& lines, const QStringList& before, const QStringList& after) const
{
    QString system = QString::fromUtf8(kSystemPrompt);
    if (!m_config.glossary.isEmpty()) {
        system += QStringLiteral("\n术语表（必须使用这些译法）：");
        for (const auto& [ja, zh] : m_config.glossary)
            system += QStringLiteral("\n%1 → %2").arg(ja, zh);
    }
    QJsonObject payload{{QStringLiteral("before"), QJsonArray::fromStringList(before)},
                        {QStringLiteral("lines"), QJsonArray::fromStringList(lines)},
                        {QStringLiteral("after"), QJsonArray::fromStringList(after)}};
    return QJsonArray{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}},
    };
}

Translator::Result Translator::post(const QJsonArray& messages, QString* content, QString* error,
                                    const std::atomic_bool* cancel)
{
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(m_config.baseUrl + QStringLiteral("/chat/completions")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_config.apiKey.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_config.apiKey.toUtf8());
    req.setTransferTimeout(m_config.timeoutMs);

    const QJsonObject body{{QStringLiteral("model"), m_config.model},
                           {QStringLiteral("messages"), messages},
                           {QStringLiteral("temperature"), 0.3},
                           {QStringLiteral("stream"), false},
                           {QStringLiteral("response_format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("json_object")}}}};
    QNetworkReply* reply = nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (cancel && cancel->load())
            reply->abort();
    });
    poll.start(200);
    loop.exec();
    reply->deleteLater();

    if (cancel && cancel->load())
        return Result::Canceled;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError || status >= 400) {
        QString detail = QJsonDocument::fromJson(data).object().value(QStringLiteral("error")).toObject()
                             .value(QStringLiteral("message")).toString();
        if (detail.isEmpty())
            detail = QString::fromUtf8(data.left(300)).trimmed();
        *error = status > 0 ? QStringLiteral("HTTP %1 %2").arg(status).arg(detail)
                            : QStringLiteral("%1 (%2)").arg(reply->errorString(), m_config.baseUrl);
        if (status == 401 || status == 402 || status == 403 || status == 404)
            return Result::Fatal; // key 无效、没余额、模型名或地址写错：重试没有意义
        if (status == 0 && (reply->error() == QNetworkReply::ConnectionRefusedError
                            || reply->error() == QNetworkReply::HostNotFoundError))
            return Result::Fatal;
        return Result::Retryable; // 429、5xx、超时
    }
    const QJsonObject obj = QJsonDocument::fromJson(data).object();
    *content = obj.value(QStringLiteral("choices")).toArray().at(0).toObject().value(QStringLiteral("message"))
                   .toObject().value(QStringLiteral("content")).toString();
    if (content->isEmpty()) {
        *error = QObject::tr("翻译服务返回了空内容");
        return Result::Retryable;
    }
    return Result::Ok;
}

Translator::Result Translator::translateBatch(const QStringList& lines, const QStringList& before,
                                              const QStringList& after, QStringList* out, QString* error,
                                              const std::atomic_bool* cancel)
{
    const QJsonArray messages = buildMessages(lines, before, after);
    for (int attempt = 0; attempt <= m_config.maxRetries; ++attempt) {
        if (attempt > 0)
            QThread::msleep(1000u << (attempt - 1)); // 1s、2s、4s
        QString content;
        const Result r = post(messages, &content, error, cancel);
        if (r == Result::Fatal || r == Result::Canceled)
            return r;
        if (r == Result::Retryable)
            continue;
        *out = parseReply(content, int(lines.size()));
        if (!out->isEmpty())
            return Result::Ok;
        *error = QObject::tr("译文条数与原文不一致");
        return Result::Retryable; // 交给调用方拆成单条重试
    }
    return Result::Retryable;
}

Translator::BatchOutcome Translator::translateLines(const QStringList& lines, const QStringList& before,
                                                   const QStringList& after, const std::atomic_bool* cancel)
{
    BatchOutcome o;
    o.zh = QStringList(lines.size(), QString());
    QString err;
    QStringList zh;
    Result r = translateBatch(lines, before, after, &zh, &err, cancel);
    if (r == Result::Ok)
        o.zh = zh;
    // 逐条补译：整批失败的行，以及模型返回空字符串的行；前后文从这一批及其上下文里取
    const QStringList all = before + lines + after;
    const int ctx = m_config.contextLines;
    for (int k = 0; k < lines.size() && r != Result::Fatal && r != Result::Canceled; ++k) {
        if (!o.zh[k].isEmpty())
            continue;
        const int i = int(before.size()) + k;
        const int from = qMax(0, i - ctx);
        QStringList one;
        r = translateBatch({lines[k]}, all.mid(from, i - from), all.mid(i + 1, ctx), &one, &err, cancel);
        if (r == Result::Ok && !one.first().isEmpty()) {
            o.zh[k] = one.first();
        } else {
            if (r == Result::Ok)
                err = tr("模型对“%1”返回了空译文").arg(lines[k]);
            o.error = err;
        }
    }
    o.fatal = r == Result::Fatal;
    o.canceled = r == Result::Canceled;
    if (o.fatal)
        o.error = err;
    return o;
}

TranslateOutcome Translator::translate(QList<Segment>* segments, const std::function<void(double)>& progress,
                                       const std::atomic_bool* cancel)
{
    TranslateOutcome outcome;
    QList<int> todo;
    for (int i = 0; i < segments->size(); ++i) {
        if ((*segments)[i].zh.trimmed().isEmpty())
            todo << i;
    }
    if (todo.isEmpty()) {
        if (progress)
            progress(1.0);
        return outcome;
    }

    auto context = [&](int from, int to) { // [from, to) 的原文
        QStringList out;
        for (int i = qMax(0, from); i < qMin(int(segments->size()), to); ++i)
            out << (*segments)[i].ja;
        return out;
    };

    // 各批互不依赖，并发请求；结果先放进 results，全部结束后再写回（工作期间 segments 只读）
    QList<QList<int>> batches;
    for (int b = 0; b < todo.size(); b += m_config.batchSize)
        batches << todo.mid(b, m_config.batchSize);
    QList<QStringList> results(batches.size());
    std::atomic_bool stopped{false}; // 出现致命错误后不再发新请求
    std::atomic_int done{0};
    QMutex mutex;

    auto runBatch = [&](int bi) {
        if (stopped || (cancel && cancel->load()))
            return;
        const QList<int>& batch = batches[bi];
        QStringList lines;
        for (int i : batch)
            lines << (*segments)[i].ja;
        const BatchOutcome b = translateLines(lines, context(batch.first() - m_config.contextLines, batch.first()),
                                              context(batch.last() + 1, batch.last() + 1 + m_config.contextLines), cancel);
        QMutexLocker lock(&mutex);
        results[bi] = b.zh;
        if (b.fatal && !outcome.fatal.isError()) {
            stopped = true;
            outcome.fatal = classifyError(b.error);
        }
        if (!b.error.isEmpty())
            outcome.lastError = b.error;
        done += int(batch.size());
        if (progress)
            progress(double(done) / todo.size());
    };

    QThreadPool pool;
    pool.setMaxThreadCount(qMax(1, m_config.parallel));
    for (int bi = 0; bi < batches.size(); ++bi)
        pool.start([&runBatch, bi] { runBatch(bi); });
    pool.waitForDone();

    for (int bi = 0; bi < batches.size(); ++bi) {
        for (int k = 0; k < results[bi].size() && k < batches[bi].size(); ++k)
            (*segments)[batches[bi][k]].zh = results[bi][k];
    }

    for (int i : todo) {
        if ((*segments)[i].zh.trimmed().isEmpty())
            ++outcome.failed;
    }
    return outcome;
}
