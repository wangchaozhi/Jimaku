#include "cli.h"

#include "core/modelmanager.h"
#include "core/pipeline.h"
#include "core/settings.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

namespace {

void err(const QString& text)
{
    std::fprintf(stderr, "%s\n", qPrintable(text));
}

bool ensureModel(const QString& id)
{
    if (ModelManager::isDownloaded(id))
        return true;
    err(QCoreApplication::translate("cli", "下载识别模型 %1 …").arg(id));
    ModelManager mm;
    bool ok = false;
    QObject::connect(&mm, &ModelManager::progress, [](qint64 got, qint64 total, double) {
        if (total > 0)
            std::fprintf(stderr, "\r  %.0f%%", 100.0 * got / total);
    });
    QObject::connect(&mm, &ModelManager::finished, [&](bool success, const QString& e) {
        ok = success;
        std::fprintf(stderr, "\n");
        if (!success)
            err(e);
        QCoreApplication::exit();
    });
    mm.download(id);
    QCoreApplication::exec();
    return ok;
}

} // namespace

int runCli(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("Jimaku：日语视频 → 中日双语字幕"));
    p.addHelpOption();
    p.addOption({QStringLiteral("cli"), QStringLiteral("无界面运行")});
    p.addOption({{QStringLiteral("o"), QStringLiteral("output")}, QStringLiteral("输出文件（默认与视频同目录）"), QStringLiteral("path")});
    p.addOption({QStringLiteral("format"), QStringLiteral("srt 或 ass"), QStringLiteral("format"), QStringLiteral("srt")});
    p.addOption({QStringLiteral("layout"), QStringLiteral("both / zh / ja"), QStringLiteral("layout"), QStringLiteral("both")});
    p.addOption({QStringLiteral("model"), QStringLiteral("识别模型：large-v3-turbo / medium / small"), QStringLiteral("id")});
    p.addOption({QStringLiteral("model-file"), QStringLiteral("直接指定 ggml 模型文件（试用其他 whisper 模型）"), QStringLiteral("path")});
    p.addOption({QStringLiteral("no-translate"), QStringLiteral("只识别，不翻译")});
    p.addOption({QStringLiteral("no-review"), QStringLiteral("翻译后不做全片校对（统一人名、纠正识别错误）")});
    p.addOption({QStringLiteral("no-cache"), QStringLiteral("不使用翻译缓存")});
    p.addOption({QStringLiteral("decode"), QStringLiteral("解码策略：greedy / nofallback / beam / beamfallback"), QStringLiteral("mode")});
    p.addOption({QStringLiteral("term"), QStringLiteral("追加翻译术语（可重复）：日文=中文"), QStringLiteral("ja=zh")});
    p.addOption({QStringLiteral("chunk"), QStringLiteral("长视频分段识别时每段的秒数（默认 300）"), QStringLiteral("seconds")});
    p.addPositionalArgument(QStringLiteral("video"), QStringLiteral("视频或音频文件"));
    p.process(app);

    if (p.positionalArguments().size() != 1) {
        err(p.helpText());
        return 1;
    }
    const QString input = QFileInfo(p.positionalArguments().first()).absoluteFilePath();
    const QString modelId = p.isSet(QStringLiteral("model")) ? ModelManager::find(p.value(QStringLiteral("model"))).id : Settings::asrModel();
    const QString modelFile = p.value(QStringLiteral("model-file"));
    if (modelFile.isEmpty() && !ensureModel(modelId))
        return 1;

    PipelineOptions o;
    o.input = input;
    o.modelPath = modelFile.isEmpty() ? ModelManager::modelPath(modelId) : QFileInfo(modelFile).absoluteFilePath();
    o.translate = !p.isSet(QStringLiteral("no-translate"));
    const QString decode = p.value(QStringLiteral("decode"));
    if (decode == QLatin1String("nofallback"))
        o.decoding = Transcriber::Decoding::GreedyNoFallback;
    else if (decode == QLatin1String("beam"))
        o.decoding = Transcriber::Decoding::BeamSearch;
    else if (decode == QLatin1String("beamfallback"))
        o.decoding = Transcriber::Decoding::BeamSearchFallback;
    if (p.isSet(QStringLiteral("chunk"))) {
        o.chunkLength = qMax(30.0, p.value(QStringLiteral("chunk")).toDouble());
        o.firstChunk = qMin(o.firstChunk, o.chunkLength);
    }
    if (o.translate) {
        o.translator = Settings::translatorConfig();
        o.review = Settings::reviewEnabled() && !p.isSet(QStringLiteral("no-review"));
        if (p.isSet(QStringLiteral("no-cache")))
            o.translator.cacheDir.clear();
        if (o.translator.apiKey.isEmpty() && Settings::provider(Settings::providerId()).needsKey) {
            err(QCoreApplication::translate("cli", "没有 %1 的 API key：在程序设置里填写，或设置环境变量 JIMAKU_API_KEY")
                    .arg(Settings::provider(Settings::providerId()).label));
            return 1;
        }
    }
    // 放在 translatorConfig() 之后，不然会被覆盖掉
    o.translator.glossary += Settings::parseGlossary(p.values(QStringLiteral("term")).join(QLatin1Char('\n')));

    const auto format = p.value(QStringLiteral("format")) == QLatin1String("ass") ? SubtitleWriter::Format::Ass : SubtitleWriter::Format::Srt;
    const QString layoutArg = p.value(QStringLiteral("layout"));
    const auto layout = layoutArg == QLatin1String("zh")   ? SubtitleWriter::Layout::ChineseOnly
                        : layoutArg == QLatin1String("ja") ? SubtitleWriter::Layout::JapaneseOnly
                                                           : SubtitleWriter::Layout::Bilingual;
    QString output = p.value(QStringLiteral("output"));
    if (output.isEmpty()) {
        const QFileInfo fi(input);
        output = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
                 + (format == SubtitleWriter::Format::Ass ? QStringLiteral(".zh.ass") : QStringLiteral(".zh.srt"));
    }

    int code = 1;
    Pipeline pipeline;
    QElapsedTimer clock;
    double firstLine = -1; // 从开始到第一行字幕出现的秒数（边处理边预览时用户要等多久）
    QObject::connect(&pipeline, &Pipeline::linesAdded, [&] {
        if (firstLine < 0)
            firstLine = clock.elapsed() / 1000.0;
    });
    QObject::connect(&pipeline, &Pipeline::stageChanged, [](Pipeline::Stage s) { err(Pipeline::stageName(s)); });
    QObject::connect(&pipeline, &Pipeline::failed, [&](const TaskError& e) {
        err(QStringLiteral("失败：%1\n提示：%2").arg(e.message, e.hint));
        QCoreApplication::exit();
    });
    QObject::connect(&pipeline, &Pipeline::canceled, [] { QCoreApplication::exit(); });
    QObject::connect(&pipeline, &Pipeline::finished, [&](const PipelineResult& r) {
        QString writeError;
        if (!SubtitleWriter::write(r.segments, output, format, layout, &writeError)) {
            err(writeError);
            QCoreApplication::exit();
            return;
        }
        double longest = 0;
        for (const Segment& s : r.segments)
            longest = qMax(longest, s.duration());
        const QJsonObject stats{
            {QStringLiteral("output"), output},
            {QStringLiteral("media_seconds"), r.mediaDuration},
            {QStringLiteral("raw_segments"), r.rawSegments},
            {QStringLiteral("lines"), int(r.segments.size())},
            {QStringLiteral("longest_line_seconds"), longest},
            {QStringLiteral("first_line_seconds"), firstLine},
            {QStringLiteral("total_seconds"), clock.elapsed() / 1000.0},
            {QStringLiteral("untranslated"), r.untranslated},
            {QStringLiteral("translate_error"), r.translateError.isError() ? r.translateError.message : r.lastTranslateError},
            {QStringLiteral("review"), QJsonObject{{QStringLiteral("summary"), r.review.summary},
                                                    {QStringLiteral("terms"), int(r.review.terms.size())},
                                                    {QStringLiteral("ja_fixed"), r.review.jaFixed},
                                                    {QStringLiteral("zh_fixed"), r.review.zhFixed},
                                                    {QStringLiteral("error"), r.review.error}}},
            {QStringLiteral("usage"), QJsonObject{{QStringLiteral("prompt_tokens"), r.usage.promptTokens},
                                                   {QStringLiteral("completion_tokens"), r.usage.completionTokens},
                                                   {QStringLiteral("requests"), r.usage.requests},
                                                   {QStringLiteral("cache_hits"), r.usage.cacheHits}}},
            {QStringLiteral("seconds"), QJsonObject{{QStringLiteral("decode"), r.decodeSeconds},
                                                     {QStringLiteral("load_model"), r.loadSeconds},
                                                     {QStringLiteral("transcribe"), r.transcribeSeconds},
                                                     {QStringLiteral("translate"), r.translateSeconds},
                                                     {QStringLiteral("review"), r.reviewSeconds}}},
        };
        std::printf("%s\n", QJsonDocument(stats).toJson(QJsonDocument::Indented).constData());
        for (const auto& [ja, zh] : r.review.terms)
            err(QStringLiteral("术语：%1 → %2").arg(ja, zh));
        if (r.translateError.isError())
            err(QStringLiteral("翻译失败：%1\n提示：%2").arg(r.translateError.message, r.translateError.hint));
        else if (o.translate && r.untranslated > 0)
            err(QStringLiteral("警告：有 %1 行没有翻译成功（%2）").arg(r.untranslated).arg(r.lastTranslateError));
        code = (o.translate && r.untranslated > 0) ? 2 : 0;
        QCoreApplication::exit();
    });
    clock.start();
    pipeline.start(o);
    QCoreApplication::exec();
    return code;
}
