#include "pipeline.h"

#include "mediadecoder.h"

#include <QElapsedTimer>

Pipeline::Pipeline(QObject* parent)
    : QObject(parent)
    , m_transcriber(std::make_unique<Transcriber>())
{
}

Pipeline::~Pipeline()
{
    cancel();
    if (m_thread)
        m_thread->wait();
}

QString Pipeline::stageName(Stage stage)
{
    switch (stage) {
    case Decoding:
        return tr("正在读取音频…");
    case LoadingModel:
        return tr("正在加载识别模型…");
    case Transcribing:
        return tr("正在识别日语…");
    case Translating:
        return tr("正在翻译成中文…");
    }
    return QString();
}

void Pipeline::launch(std::function<void()> work)
{
    if (isRunning())
        return;
    m_cancel = false;
    m_thread = QThread::create(std::move(work));
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}

void Pipeline::start(const PipelineOptions& options)
{
    launch([this, options] { runAll(options); });
}

void Pipeline::retranslate(const QList<Segment>& segments, const TranslatorConfig& config)
{
    launch([this, segments, config] {
        emit stageChanged(Translating);
        emit progressChanged(0);
        PipelineResult result;
        result.segments = segments;
        QElapsedTimer timer;
        timer.start();
        Translator translator(config);
        const TranslateOutcome t = translator.translate(&result.segments, [this](double p) { emit progressChanged(p); }, &m_cancel);
        if (m_cancel) {
            emit canceled();
            return;
        }
        result.translateSeconds = timer.elapsed() / 1000.0;
        result.untranslated = t.failed;
        result.translateError = t.fatal;
        result.lastTranslateError = t.lastError;
        emit finished(result);
    });
}

void Pipeline::cancel()
{
    m_cancel = true;
}

void Pipeline::runAll(const PipelineOptions& o)
{
    // 各阶段在总进度里的占比
    const double wDecode = 0.05, wLoad = 0.05;
    const double wAsr = o.translate ? 0.6 : 0.9;
    const double base[] = {0, wDecode, wDecode + wLoad, wDecode + wLoad + wAsr};
    const double weight[] = {wDecode, wLoad, wAsr, 1.0 - (wDecode + wLoad + wAsr)};
    auto report = [this, &base, &weight](Stage s, double local) { emit progressChanged(base[s] + weight[s] * local); };
    auto enter = [this, &report](Stage s) {
        emit stageChanged(s);
        report(s, 0);
    };

    PipelineResult result;
    QElapsedTimer timer;
    QString error;

    // 1. 解码
    enter(Decoding);
    timer.start();
    result.mediaDuration = MediaDecoder::probeDuration(o.input, &error);
    std::vector<float> pcm;
    if (result.mediaDuration < 0
        || !MediaDecoder::decodePcm(o.input, result.mediaDuration, &pcm, [&](double p) { report(Decoding, p); }, &m_cancel, &error)) {
        if (m_cancel)
            emit canceled();
        else
            emit failed(classifyError(error));
        return;
    }
    result.decodeSeconds = timer.restart() / 1000.0;

    // 2. 加载模型（已加载则直接复用）
    enter(LoadingModel);
    result.modelReused = m_transcriber->isLoaded() && m_transcriber->modelPath() == o.modelPath;
    if (!m_transcriber->load(o.modelPath, &error)) {
        emit failed(makeError(TaskError::Model, error));
        return;
    }
    result.loadSeconds = timer.restart() / 1000.0;

    // 3. 识别 + 断句
    enter(Transcribing);
    const QList<Segment> raw = m_transcriber->transcribe(pcm, [&](double p) { report(Transcribing, p); }, &m_cancel, &error);
    if (m_cancel) {
        emit canceled();
        return;
    }
    if (raw.isEmpty()) {
        emit failed(error.isEmpty() ? makeError(TaskError::Input, tr("没有识别到日语语音")) : classifyError(error));
        return;
    }
    pcm.clear();
    pcm.shrink_to_fit();
    result.rawSegments = int(raw.size());
    result.segments = resegment(raw, o.segmenter);
    result.transcribeSeconds = timer.restart() / 1000.0;

    // 4. 翻译：失败不丢弃识别结果，交给界面提示和重试
    if (o.translate) {
        enter(Translating);
        Translator translator(o.translator);
        const TranslateOutcome t = translator.translate(&result.segments, [&](double p) { report(Translating, p); }, &m_cancel);
        if (m_cancel) {
            emit canceled();
            return;
        }
        result.untranslated = t.failed;
        result.translateError = t.fatal;
        result.lastTranslateError = t.lastError;
        result.translateSeconds = timer.restart() / 1000.0;
    } else {
        result.untranslated = int(result.segments.size());
    }
    emit progressChanged(1.0);
    emit finished(result);
}
