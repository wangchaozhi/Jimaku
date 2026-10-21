#include "pipeline.h"

#include "mediadecoder.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QThreadPool>

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
    case Reviewing:
        return tr("正在统一人名、校对识别错误…");
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
        result.usage = translator.usage();
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
    const double base[] = {0, wDecode, wDecode + wLoad, wDecode + wLoad + wAsr, 0.95};
    const double weight[] = {wDecode, wLoad, wAsr, 0.95 - (wDecode + wLoad + wAsr), 0.05};
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

    // 3. 识别 + 断句 + 翻译同时进行：whisper 每出几段就断句定稿，攒够一批就交给线程池翻译
    enter(Transcribing);
    IncrementalSegmenter segmenter(o.segmenter);
    Translator translator(o.translator);
    const int batch = qMin(10, o.translator.batchSize); // 小批：字幕更快出现译文
    const int ctx = o.translator.contextLines;
    QList<Segment>& lines = result.segments; // 只在本线程追加；翻译任务拿的是拷贝
    int submitted = 0;
    QThreadPool pool;
    pool.setMaxThreadCount(qMax(1, o.translator.parallel));
    QMutex mutex; // 保护 zh 结果和错误信息
    QHash<int, QString> zh;
    std::atomic_bool stopped{false};
    std::atomic_int translatedCount{0};

    auto text = [&](int from, int to) {
        QStringList out;
        for (int i = qMax(0, from); i < qMin(int(lines.size()), to); ++i)
            out << lines[i].ja;
        return out;
    };
    // 后文也要齐了才提交（识别结束时把剩下的全部提交）
    auto submit = [&](bool final) {
        if (!o.translate)
            return;
        const int slack = 6; // 为了让一批在句末结束，最多多带几行
        while (submitted < lines.size() && (final || submitted + batch + slack + ctx <= lines.size())) {
            const int first = submitted;
            int last = qMin(int(lines.size()), first + batch);
            // 一句话不要被切在两批里：延伸到句末（整句翻译、再按行拆分）
            while (last < lines.size() && last - first < batch + slack && !Translator::endsSentence(lines[last - 1].ja))
                ++last;
            submitted = last;
            const QStringList src = text(first, last), before = text(first - ctx, first), after = text(last, last + ctx);
            pool.start([&, first, src, before, after] {
                if (stopped || m_cancel)
                    return;
                const Translator::BatchOutcome b = translator.translateLines(src, before, after, &m_cancel);
                {
                    QMutexLocker lock(&mutex);
                    for (int k = 0; k < b.zh.size(); ++k)
                        zh.insert(first + k, b.zh[k]);
                    if (b.fatal && !result.translateError.isError()) {
                        stopped = true;
                        result.translateError = classifyError(b.error);
                    }
                    if (!b.error.isEmpty())
                        result.lastTranslateError = b.error;
                }
                translatedCount += int(src.size());
                emit translated(first, b.zh);
            });
        }
    };
    auto add = [&](const QList<Segment>& fresh) {
        if (fresh.isEmpty())
            return;
        const int first = int(lines.size());
        lines += fresh;
        emit linesAdded(first, fresh);
        submit(false);
    };

    const QList<Segment> raw = m_transcriber->transcribe(
        pcm, [&](double p) { report(Transcribing, p); }, &m_cancel, &error,
        [&](const QList<Segment>& fresh) {
            add(segmenter.feed(fresh));
            emit transcribedUntil(fresh.last().end);
        });
    if (m_cancel) {
        pool.waitForDone();
        emit canceled();
        return;
    }
    if (raw.isEmpty()) {
        pool.waitForDone();
        emit failed(error.isEmpty() ? makeError(TaskError::Input, tr("没有识别到日语语音")) : classifyError(error));
        return;
    }
    add(segmenter.finish());
    emit transcribedUntil(result.mediaDuration);
    pcm.clear();
    pcm.shrink_to_fit();
    result.rawSegments = int(raw.size());
    result.transcribeSeconds = timer.restart() / 1000.0;

    // 4. 等剩下的翻译完成：失败不丢弃识别结果，交给界面提示和重试
    if (o.translate) {
        enter(Translating);
        submit(true);
        while (!pool.waitForDone(200)) {
            if (!lines.isEmpty())
                report(Translating, double(translatedCount) / lines.size());
        }
        if (m_cancel) {
            emit canceled();
            return;
        }
        for (auto it = zh.cbegin(); it != zh.cend(); ++it)
            lines[it.key()].zh = it.value();
        result.translateSeconds = timer.restart() / 1000.0;

        // 5. 全片校对：统一专有名词、纠正识别错误（失败不影响已有译文）
        if (o.review && !result.translateError.isError() && !lines.isEmpty()) {
            enter(Reviewing);
            result.review = translator.review(&lines, &m_cancel);
            if (m_cancel) {
                emit canceled();
                return;
            }
            if (result.review.jaFixed + result.review.zhFixed > 0)
                emit segmentsUpdated(lines);
            result.reviewSeconds = timer.restart() / 1000.0;
        }
        for (const Segment& s : lines)
            result.untranslated += s.zh.trimmed().isEmpty() ? 1 : 0;
        result.usage = translator.usage();
    } else {
        result.untranslated = int(lines.size());
    }
    emit progressChanged(1.0);
    emit finished(result);
}
