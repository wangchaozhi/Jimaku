#include "cli.h"

#include "core/modelmanager.h"
#include "core/pipeline.h"
#include "core/settings.h"

#include <QCommandLineParser>
#include <QCoreApplication>
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
    p.addOption({QStringLiteral("no-translate"), QStringLiteral("只识别，不翻译")});
    p.addPositionalArgument(QStringLiteral("video"), QStringLiteral("视频或音频文件"));
    p.process(app);

    if (p.positionalArguments().size() != 1) {
        err(p.helpText());
        return 1;
    }
    const QString input = QFileInfo(p.positionalArguments().first()).absoluteFilePath();
    const QString modelId = p.isSet(QStringLiteral("model")) ? ModelManager::find(p.value(QStringLiteral("model"))).id : Settings::asrModel();
    if (!ensureModel(modelId))
        return 1;

    PipelineOptions o;
    o.input = input;
    o.modelPath = ModelManager::modelPath(modelId);
    o.translate = !p.isSet(QStringLiteral("no-translate"));
    if (o.translate) {
        o.translator = Settings::translatorConfig();
        if (o.translator.apiKey.isEmpty() && Settings::provider(Settings::providerId()).needsKey) {
            err(QCoreApplication::translate("cli", "没有 %1 的 API key：在程序设置里填写，或设置环境变量 JIMAKU_API_KEY")
                    .arg(Settings::provider(Settings::providerId()).label));
            return 1;
        }
    }

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
            {QStringLiteral("untranslated"), r.untranslated},
            {QStringLiteral("translate_error"), r.translateError.isError() ? r.translateError.message : r.lastTranslateError},
            {QStringLiteral("seconds"), QJsonObject{{QStringLiteral("decode"), r.decodeSeconds},
                                                     {QStringLiteral("load_model"), r.loadSeconds},
                                                     {QStringLiteral("transcribe"), r.transcribeSeconds},
                                                     {QStringLiteral("translate"), r.translateSeconds}}},
        };
        std::printf("%s\n", QJsonDocument(stats).toJson(QJsonDocument::Indented).constData());
        if (r.translateError.isError())
            err(QStringLiteral("翻译失败：%1\n提示：%2").arg(r.translateError.message, r.translateError.hint));
        else if (o.translate && r.untranslated > 0)
            err(QStringLiteral("警告：有 %1 行没有翻译成功（%2）").arg(r.untranslated).arg(r.lastTranslateError));
        code = (o.translate && r.untranslated > 0) ? 2 : 0;
        QCoreApplication::exit();
    });
    pipeline.start(o);
    QCoreApplication::exec();
    return code;
}
