#include "translator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <algorithm>

namespace {

// 提示词改了就把版本号加一，旧缓存自然失效
const char* kPromptVersion = "v2";

const char* kTranslatePrompt =
    "你是专业的日译中字幕译者。用户发来一个 JSON：lines 是需要翻译的日文字幕（按播放顺序，每条有序号 i 和句号 s），"
    "before / after 是前后文，只用来理解语境，不要翻译。\n"
    "要求：\n"
    "1. 译成自然、口语化的简体中文（不要用繁体字），符合字幕阅读习惯，不加任何解释或注释。\n"
    "2. s 相同的连续几条是同一句话被切成了几段：先把整句理解并翻译成通顺的中文，"
    "再把译文按中文语序分成同样多段，依次对应这几条，每段尽量对应该条日文所说的内容；每段都不能为空。\n"
    "3. 输出条数和顺序必须与 lines 完全一致，不得合并或遗漏。\n"
    "4. 原文来自语音识别，可能有同音错字，请结合上下文按正确的意思翻译；人名、团体名前后保持一致，外来语用通行译名。"
    "听不清或不认识的人名照原文保留，不要编造名字。\n";

const char* kReviewPrompt =
    "你是日语视频字幕的校对。用户发来整段视频的字幕 JSON：lines 里每条有序号 i、语音识别得到的日文 ja、机器翻译的中文 zh；"
    "glossary 是已经确定的译法。\n"
    "任务：\n"
    "1. 找出人名、团体名、作品名、品牌等专有名词，确定统一的中文译法（已在 glossary 里的照用）。\n"
    "2. 结合上下文找出明显的语音识别错误（同音错字、外来语听错），给出纠正后的日文。\n"
    "3. 修正受上面两点影响、或者同一个名词前后译法不一致的中文译文。中文用简体。\n"
    "严格限制：\n"
    "- 只改有把握的地方；拿不准就不改。宁可漏改，不要改错。\n"
    "- 绝不能把一个人的名字换成另一个人的名字；不认识的名字保留原样。\n"
    "- 每行只改这一行自己的内容，不要把相邻行的内容并进来，也不要删掉这一行原有的意思。\n"
    "- 不要为了润色而改写译文。\n"
    "只列出需要修改的行，不需要改的行不要输出。只输出 JSON：\n"
    "{\"summary\": \"一句话概括视频内容\", \"terms\": [{\"ja\": \"日文\", \"zh\": \"中文\"}], "
    "\"fixes\": [{\"i\": 序号, \"ja\": \"纠正后的日文（不改则省略）\", \"zh\": \"修改后的中文（不改则省略）\"}]}";

const char* kTermsPrompt =
    "从下面的日语视频字幕里找出人名、团体名、作品名、地名、品牌等专有名词，给出统一的中文译法。"
    "每条字幕是 \"日文 ｜ 现有中文译文\"（还没翻译时只有日文）。"
    "现有译文里已经有的译法优先沿用其中最常见的一种；不认识的人名照原文保留，不要编造；拿不准的不要列出。"
    "glossary 里已经确定的不用重复。只输出 JSON：{\"terms\": [{\"ja\": \"日文\", \"zh\": \"中文\"}]}";

constexpr int kReviewChunk = 150; // 校对时每次交给模型的行数
constexpr int kTermsChunk = 600;  // 提取术语时每次交给模型的行数

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

// 从回复里取出 JSON 文本：去掉 ``` 外壳和前后多余的说明
QString stripJson(const QString& content, QChar open, QChar close)
{
    QString text = content.trimmed();
    static const QRegularExpression fence(QStringLiteral("```(?:json)?\\s*([\\s\\S]*?)```"));
    const auto m = fence.match(text);
    if (m.hasMatch())
        text = m.captured(1).trimmed();
    const int start = text.indexOf(open);
    const int end = text.lastIndexOf(close);
    return start >= 0 && end > start ? text.mid(start, end - start + 1) : QString();
}

} // namespace

bool Translator::endsSentence(const QString& ja)
{
    const QString t = ja.trimmed();
    static const QString marks = QStringLiteral("。！？!?…♪");
    return !t.isEmpty() && marks.contains(t.back());
}

Translator::Translator(TranslatorConfig config)
    : m_config(std::move(config))
{
    while (m_config.baseUrl.endsWith(QLatin1Char('/')))
        m_config.baseUrl.chop(1);
}

TranslateUsage Translator::usage() const
{
    return {m_promptTokens.load(), m_completionTokens.load(), m_requests.load(), m_cacheHits.load()};
}

QStringList Translator::parseReply(const QString& content, int expected)
{
    QStringList result;
    const QString trimmed = content.trimmed();
    const int objStart = trimmed.indexOf(QLatin1Char('{'));
    const int arrStart = trimmed.indexOf(QLatin1Char('['));
    if (objStart >= 0 && (arrStart < 0 || objStart < arrStart)) {
        const QJsonObject obj = QJsonDocument::fromJson(stripJson(content, QLatin1Char('{'), QLatin1Char('}')).toUtf8()).object();
        for (const char* key : {"t", "translations", "lines"}) {
            if (obj.value(QLatin1String(key)).isArray()) {
                result = toStringList(obj.value(QLatin1String(key)).toArray());
                break;
            }
        }
    } else if (arrStart >= 0) {
        result = toStringList(QJsonDocument::fromJson(stripJson(content, QLatin1Char('['), QLatin1Char(']')).toUtf8()).array());
    }
    return result.size() == expected ? result : QStringList();
}

QList<int> Translator::sentenceGroups(const QStringList& lines)
{
    QList<int> groups;
    int g = 0;
    for (int i = 0; i < lines.size(); ++i) {
        groups << g;
        if (endsSentence(lines[i]))
            ++g;
    }
    return groups;
}

QString Translator::applySubtitleStyle(const QString& zh)
{
    QString s = zh.trimmed();
    static const QString trailing = QStringLiteral("。，、,.；;：:");
    while (!s.isEmpty() && trailing.contains(s.back()))
        s.chop(1);
    static const QRegularExpression inner(QStringLiteral("\\s*[，、；。]\\s*"));
    s.replace(inner, QStringLiteral(" "));
    static const QRegularExpression spaces(QStringLiteral("\\s{2,}"));
    s.replace(spaces, QStringLiteral(" "));
    return s.trimmed();
}

Translator::ParsedReview Translator::parseReview(const QString& content, int firstIndex, int lineCount)
{
    ParsedReview r;
    const QJsonDocument doc = QJsonDocument::fromJson(stripJson(content, QLatin1Char('{'), QLatin1Char('}')).toUtf8());
    if (!doc.isObject())
        return r;
    const QJsonObject obj = doc.object();
    r.ok = true;
    r.summary = obj.value(QStringLiteral("summary")).toString().trimmed();
    for (const QJsonValue& v : obj.value(QStringLiteral("terms")).toArray()) {
        const QString ja = v.toObject().value(QStringLiteral("ja")).toString().trimmed();
        const QString zh = v.toObject().value(QStringLiteral("zh")).toString().trimmed();
        if (!ja.isEmpty() && !zh.isEmpty())
            r.terms.append({ja, zh});
    }
    for (const QJsonValue& v : obj.value(QStringLiteral("fixes")).toArray()) {
        const QJsonObject f = v.toObject();
        ReviewFix fix;
        fix.index = f.value(QStringLiteral("i")).toInt(-1);
        fix.ja = f.value(QStringLiteral("ja")).toString().trimmed();
        fix.zh = f.value(QStringLiteral("zh")).toString().trimmed();
        if (fix.index < firstIndex || fix.index >= firstIndex + lineCount || (fix.ja.isEmpty() && fix.zh.isEmpty()))
            continue;
        r.fixes.append(fix);
    }
    return r;
}

QJsonArray Translator::buildMessages(const QStringList& lines, const QStringList& before, const QStringList& after) const
{
    QString system = QString::fromUtf8(kTranslatePrompt);
    system += m_config.translateLyrics ? QStringLiteral("5. 英文歌词、英文口号也翻译成中文。\n")
                                       : QStringLiteral("5. 英文歌词、英文口号保留原文，不翻译。\n");
    system += QStringLiteral("日语的语气词、感叹词、应答（うん、あー、いいね、はい 等）一定要译成对应的中文（嗯、啊、不错、是），不要原样返回日文。\n");
    if (m_config.subtitleStyle)
        system += QStringLiteral("6. 每段译文尽量简短，一般不超过 16 个汉字。\n");
    const QList<QPair<QString, QString>> glossary = this->glossary();
    if (!glossary.isEmpty()) {
        system += QStringLiteral("术语表（必须使用这些译法）：");
        for (const auto& [ja, zh] : glossary)
            system += QStringLiteral("\n%1 → %2").arg(ja, zh);
        system += QLatin1Char('\n');
    }
    system += QStringLiteral("只输出 JSON：{\"t\": [\"第1条译文\", \"第2条译文\", ...]}");

    const QList<int> groups = sentenceGroups(lines);
    QJsonArray items;
    for (int k = 0; k < lines.size(); ++k)
        items.append(QJsonObject{{QStringLiteral("i"), k}, {QStringLiteral("s"), groups[k]}, {QStringLiteral("ja"), lines[k]}});
    const QJsonObject payload{{QStringLiteral("before"), QJsonArray::fromStringList(before)},
                              {QStringLiteral("lines"), items},
                              {QStringLiteral("after"), QJsonArray::fromStringList(after)}};
    return QJsonArray{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}},
    };
}

Translator::Result Translator::post(const QJsonArray& messages, double temperature, QString* content, QString* error,
                                    const std::atomic_bool* cancel)
{
    const QJsonObject body{{QStringLiteral("model"), m_config.model},
                           {QStringLiteral("messages"), messages},
                           {QStringLiteral("temperature"), temperature},
                           {QStringLiteral("stream"), false},
                           {QStringLiteral("response_format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("json_object")}}}};
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    // 缓存：同样的请求（同一服务、模型、提示词版本和内容）直接用上次的回复
    QString cacheFile;
    if (!m_config.cacheDir.isEmpty()) {
        QCryptographicHash h(QCryptographicHash::Sha1);
        h.addData(kPromptVersion);
        h.addData(m_config.baseUrl.toUtf8());
        h.addData(payload);
        cacheFile = m_config.cacheDir + QLatin1Char('/') + QString::fromLatin1(h.result().toHex()) + QStringLiteral(".txt");
        QFile f(cacheFile);
        if (f.open(QIODevice::ReadOnly)) {
            *content = QString::fromUtf8(f.readAll());
            if (!content->isEmpty()) {
                ++m_cacheHits;
                return Result::Ok;
            }
        }
    }

    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(m_config.baseUrl + QStringLiteral("/chat/completions")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_config.apiKey.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_config.apiKey.toUtf8());
    req.setTransferTimeout(m_config.timeoutMs);
    QNetworkReply* reply = nam.post(req, payload);
    ++m_requests;

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
    const QJsonObject usage = obj.value(QStringLiteral("usage")).toObject();
    m_promptTokens += usage.value(QStringLiteral("prompt_tokens")).toInteger();
    m_completionTokens += usage.value(QStringLiteral("completion_tokens")).toInteger();
    *content = obj.value(QStringLiteral("choices")).toArray().at(0).toObject().value(QStringLiteral("message"))
                   .toObject().value(QStringLiteral("content")).toString();
    if (content->isEmpty()) {
        *error = QObject::tr("翻译服务返回了空内容");
        return Result::Retryable;
    }
    if (!cacheFile.isEmpty() && QDir().mkpath(m_config.cacheDir)) {
        QSaveFile f(cacheFile);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(content->toUtf8());
            f.commit();
        }
    }
    return Result::Ok;
}

QList<QPair<QString, QString>> Translator::glossary() const
{
    QMutexLocker lock(&m_termsMutex);
    return m_config.glossary + m_terms;
}

QList<QPair<QString, QString>> Translator::terms() const
{
    QMutexLocker lock(&m_termsMutex);
    return m_terms;
}

void Translator::addTerms(const QList<QPair<QString, QString>>& terms)
{
    QMutexLocker lock(&m_termsMutex);
    for (const auto& t : terms) {
        const auto same = [&](const QPair<QString, QString>& k) { return k.first == t.first; };
        if (std::none_of(m_config.glossary.cbegin(), m_config.glossary.cend(), same)
            && std::none_of(m_terms.cbegin(), m_terms.cend(), same))
            m_terms.append(t);
    }
}

QString Translator::complete(const QJsonArray& messages, double temperature, QString* error, const std::atomic_bool* cancel,
                             bool* fatal)
{
    QString content;
    Result r = Result::Retryable;
    for (int attempt = 0; attempt <= m_config.maxRetries && r == Result::Retryable; ++attempt) {
        if (attempt > 0)
            QThread::msleep(1000u << (attempt - 1));
        r = post(messages, temperature, &content, error, cancel);
    }
    if (fatal)
        *fatal = r == Result::Fatal;
    return r == Result::Ok ? content : QString();
}

QList<QPair<QString, QString>> Translator::extractTerms(const QList<Segment>& lines, const std::atomic_bool* cancel,
                                                        QString* error)
{
    QList<QPair<QString, QString>> found;
    for (int first = 0; first < lines.size(); first += kTermsChunk) {
        if (cancel && cancel->load())
            break;
        QStringList text;
        for (int i = first; i < qMin(int(lines.size()), first + kTermsChunk); ++i)
            text << (lines[i].zh.isEmpty() ? lines[i].ja : lines[i].ja + QStringLiteral(" ｜ ") + lines[i].zh);
        QJsonArray known;
        for (const auto& [ja, zh] : glossary())
            known.append(QJsonObject{{QStringLiteral("ja"), ja}, {QStringLiteral("zh"), zh}});
        const QJsonObject payload{{QStringLiteral("glossary"), known}, {QStringLiteral("lines"), QJsonArray::fromStringList(text)}};
        const QJsonArray messages{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), QString::fromUtf8(kTermsPrompt)}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}},
        };
        QString err;
        bool fatal = false;
        const QString content = complete(messages, 0.2, &err, cancel, &fatal);
        if (content.isEmpty()) {
            if (error)
                *error = err;
            if (fatal)
                break;
            continue;
        }
        const ParsedReview parsed = parseReview(content, 0, 0); // 同样的 {"terms": [...]} 结构
        addTerms(parsed.terms);
        found += parsed.terms;
    }
    return found;
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
        const Result r = post(messages, 0.3, &content, error, cancel);
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

QString Translator::finish(const QString& zh) const
{
    return m_config.subtitleStyle ? applySubtitleStyle(zh) : zh.trimmed();
}

Translator::BatchOutcome Translator::translateLines(const QStringList& lines, const QStringList& before,
                                                   const QStringList& after, const std::atomic_bool* cancel)
{
    BatchOutcome o;
    o.zh = QStringList(lines.size(), QString());
    QString err;
    QStringList zh;
    Result r = translateBatch(lines, before, after, &zh, &err, cancel);
    if (r == Result::Ok) {
        for (int k = 0; k < lines.size(); ++k)
            o.zh[k] = finish(zh[k]);
    }
    // 逐条补译：整批失败的行，以及模型返回空字符串的行；前后文从这一批及其上下文里取
    const QStringList all = before + lines + after;
    const int ctx = m_config.contextLines;
    for (int k = 0; k < lines.size() && r != Result::Fatal && r != Result::Canceled; ++k) {
        Segment probe;
        probe.ja = lines[k];
        probe.zh = o.zh[k];
        if (!needsTranslation(probe))
            continue;
        o.zh[k].clear(); // 模型原样返回了日文：当作没翻出来，下面单条重试
        const int i = int(before.size()) + k;
        const int from = qMax(0, i - ctx);
        QStringList one;
        r = translateBatch({lines[k]}, all.mid(from, i - from), all.mid(i + 1, ctx), &one, &err, cancel);
        if (r == Result::Ok && !finish(one.first()).isEmpty()) {
            o.zh[k] = finish(one.first());
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

ReviewOutcome Translator::review(QList<Segment>* segments, const std::atomic_bool* cancel)
{
    ReviewOutcome out;
    // 第一步：整片统一专有名词（长视频分几次，前面定下的带到后面），之后各段校对都用这份术语表
    const QList<QPair<QString, QString>> before = terms();
    extractTerms(*segments, cancel, &out.error);
    for (const auto& t : terms()) {
        if (!before.contains(t))
            out.terms.append(t);
    }
    if (cancel && cancel->load())
        return out;

    // 第二步：各段并行校对，结果全部回来后再写回（工作期间 segments 只读）
    QJsonArray glossaryJson;
    for (const auto& [ja, zh] : glossary())
        glossaryJson.append(QJsonObject{{QStringLiteral("ja"), ja}, {QStringLiteral("zh"), zh}});
    const int chunks = int((segments->size() + kReviewChunk - 1) / kReviewChunk);
    QList<ParsedReview> results(chunks);
    std::atomic_bool stopped{false};
    QMutex mutex;
    auto runChunk = [&](int c) {
        if (stopped || (cancel && cancel->load()))
            return;
        const int first = c * kReviewChunk;
        const int count = qMin(kReviewChunk, int(segments->size()) - first);
        QJsonArray items;
        for (int i = first; i < first + count; ++i) {
            const Segment& s = (*segments)[i];
            items.append(QJsonObject{{QStringLiteral("i"), i}, {QStringLiteral("ja"), s.ja}, {QStringLiteral("zh"), s.zh}});
        }
        const QJsonObject payload{{QStringLiteral("glossary"), glossaryJson}, {QStringLiteral("lines"), items}};
        const QJsonArray messages{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), QString::fromUtf8(kReviewPrompt)}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}},
        };
        QString err;
        bool fatal = false;
        const QString content = complete(messages, 0.2, &err, cancel, &fatal);
        const ParsedReview parsed = content.isEmpty() ? ParsedReview{} : parseReview(content, first, count);
        QMutexLocker lock(&mutex);
        if (fatal)
            stopped = true;
        if (!parsed.ok)
            out.error = content.isEmpty() ? err : tr("校对结果无法解析");
        results[c] = parsed;
    };
    QThreadPool pool;
    pool.setMaxThreadCount(qMax(1, m_config.parallel));
    for (int c = 0; c < chunks; ++c)
        pool.start([&runChunk, c] { runChunk(c); });
    pool.waitForDone();

    for (const ParsedReview& parsed : results) {
        if (out.summary.isEmpty())
            out.summary = parsed.summary;
        for (const ReviewFix& f : parsed.fixes) {
            Segment& s = (*segments)[f.index];
            if (!f.ja.isEmpty() && f.ja != s.ja) {
                if (s.jaOriginal.isEmpty())
                    s.jaOriginal = s.ja;
                s.ja = f.ja;
                ++out.jaFixed;
            }
            const QString zh = finish(f.zh);
            if (!zh.isEmpty() && zh != s.zh) {
                if (s.zhOriginal.isEmpty())
                    s.zhOriginal = s.zh;
                s.zh = zh;
                ++out.zhFixed;
            }
        }
    }
    return out;
}

TranslateOutcome Translator::translate(QList<Segment>* segments, const std::function<void(double)>& progress,
                                       const std::atomic_bool* cancel)
{
    TranslateOutcome outcome;
    QList<int> todo;
    for (int i = 0; i < segments->size(); ++i) {
        if (needsTranslation((*segments)[i]))
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
        if (needsTranslation((*segments)[i]))
            ++outcome.failed;
    }
    return outcome;
}
