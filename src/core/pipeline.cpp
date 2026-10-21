#include "pipeline.h"

#include "mediadecoder.h"
#include "sleepguard.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QThreadPool>
#include <cmath>
#include <map>
#include <numeric>

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

void Pipeline::prioritize(double seconds)
{
    m_priority = seconds;
}

void Pipeline::runAll(const PipelineOptions& o)
{
    SleepGuard awake(tr("Jimaku 正在生成字幕"));
    // 各阶段在总进度里的占比
    const double wDecode = 0.02, wLoad = 0.03;
    const double wAsr = o.translate ? 0.8 : 0.95;
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

    // 1. 读时长（不再整段解码：长视频按段解码，内存只占一段）
    enter(Decoding);
    timer.start();
    result.mediaDuration = MediaDecoder::probeDuration(o.input, &error);
    if (result.mediaDuration < 0) {
        emit failed(classifyError(error));
        return;
    }
    result.decodeSeconds = timer.restart() / 1000.0;
    emit durationKnown(result.mediaDuration);

    // 2. 加载模型（已加载则直接复用）
    enter(LoadingModel);
    result.modelReused = m_transcriber->isLoaded() && m_transcriber->modelPath() == o.modelPath;
    if (!m_transcriber->load(o.modelPath, &error)) {
        emit failed(makeError(TaskError::Model, error));
        return;
    }
    result.loadSeconds = timer.restart() / 1000.0;
    m_transcriber->setDecoding(o.decoding);

    // 3. 分段识别 + 断句 + 翻译同时进行
    enter(Transcribing);
    Coverage coverage(result.mediaDuration);
    for (const TimeRange& r : o.resumeCovered)
        coverage.add(r.first, r.second);
    QList<Segment> all; // 下标就是 id；只在本线程追加，翻译任务拿的是拷贝
    for (Segment s : o.resumeSegments) {
        s.id = int(all.size());
        all.append(s);
    }
    result.resumed = !all.isEmpty() || !o.resumeCovered.isEmpty();
    emit coverageChanged(coverage.ranges());

    Translator translator(o.translator);
    const int batch = qMin(10, o.translator.batchSize); // 小批：字幕更快出现译文
    const int slack = 6;                                // 为了让一批在句末结束，最多多带几行
    const int ctx = o.translator.contextLines;
    QThreadPool pool;
    pool.setMaxThreadCount(qMax(1, o.translator.parallel));
    QMutex mutex; // 保护 zh 结果和错误信息
    QHash<int, QString> zh;
    std::atomic_bool stopped{false};
    bool termsStarted = false;
    QList<int> pending; // 已定稿、还没提交翻译的行
    if (o.translate) {
        for (const Segment& s : all) {
            if (s.zh.trimmed().isEmpty())
                pending << s.id;
        }
    }

    // 按时间排好的 id，用来取前后文
    auto sortedIds = [&] {
        QList<int> ids(all.size());
        std::iota(ids.begin(), ids.end(), 0);
        std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) { return all[a].start < all[b].start; });
        return ids;
    };
    auto submit = [&](bool flush) {
        if (!o.translate || stopped || pending.isEmpty())
            return;
        std::sort(pending.begin(), pending.end(), [&](int a, int b) { return all[a].start < all[b].start; });
        const QList<int> order = sortedIds();
        QHash<int, int> pos;
        for (int i = 0; i < order.size(); ++i)
            pos.insert(order[i], i);
        auto text = [&](int from, int to) {
            QStringList out;
            for (int i = qMax(0, from); i < qMin(int(order.size()), to); ++i)
                out << all[order[i]].ja;
            return out;
        };
        while (!pending.isEmpty() && (flush || pending.size() >= batch + slack + ctx)) {
            int n = qMin(int(pending.size()), batch);
            // 一句话不要被切在两批里：延伸到句末（整句翻译、再按行拆分）
            while (n < pending.size() && n < batch + slack && !Translator::endsSentence(all[pending[n - 1]].ja))
                ++n;
            const QList<int> ids = pending.mid(0, n);
            pending.remove(0, n);
            QStringList src;
            for (int id : ids)
                src << all[id].ja;
            const int first = pos.value(ids.first()), last = pos.value(ids.last());
            const QStringList before = text(first - ctx, first), after = text(last + 1, last + 1 + ctx);
            pool.start([&, ids, src, before, after] {
                if (stopped || m_cancel)
                    return;
                const Translator::BatchOutcome b = translator.translateLines(src, before, after, &m_cancel);
                {
                    QMutexLocker lock(&mutex);
                    for (int k = 0; k < b.zh.size(); ++k)
                        zh.insert(ids[k], b.zh[k]);
                    if (b.fatal && !result.translateError.isError()) {
                        stopped = true;
                        result.translateError = classifyError(b.error);
                    }
                    if (!b.error.isEmpty())
                        result.lastTranslateError = b.error;
                }
                emit translated(ids, b.zh);
            });
        }
    };
    auto add = [&](QList<Segment> fresh) {
        if (fresh.isEmpty())
            return;
        for (Segment& s : fresh) {
            s.id = int(all.size());
            all.append(s);
            if (o.translate)
                pending << s.id;
        }
        emit linesAdded(fresh);
        // 长视频：攒够 60 行就先提取一次人名等专有名词，之后的翻译都用统一译法
        if (o.translate && !termsStarted && all.size() >= 60) {
            termsStarted = true;
            const QList<Segment> snapshot = all;
            pool.start([&, snapshot] {
                if (!stopped && !m_cancel)
                    translator.extractTerms(snapshot, &m_cancel);
            });
        }
        submit(false);
    };

    // 每个连续的识别区间各用一个断句器（跳到后面识别时会出现多个区间），按区间当前的结尾找
    std::map<qint64, IncrementalSegmenter> segmenters;
    std::map<qint64, QList<Segment>> recentRaw; // 每个区间最近的原始分段，用来去掉跨批的重复
    auto key = [](double t) { return qint64(std::llround(t * 1000)); };
    const double margin = 15; // 在目标长度前后 15 秒内找最安静的地方切
    const int sr = MediaDecoder::kSampleRate;

    while (true) {
        if (m_cancel) {
            pool.waitForDone();
            emit canceled();
            return;
        }
        const double priority = m_priority.load();
        if (priority >= 0 && coverage.covers(priority))
            m_priority = -1;
        const double start = coverage.nextStart(m_priority.load());
        if (start < 0)
            break;
        const double limit = coverage.limitAfter(start);
        const double target = coverage.coveredSeconds() <= 0 ? o.firstChunk : o.chunkLength;
        const bool toLimit = start + target + margin >= limit;
        const double length = toLimit ? limit - start : target + margin;

        std::vector<float> pcm;
        if (!MediaDecoder::decodeRange(o.input, start, length, &pcm, &m_cancel, &error)) {
            pool.waitForDone();
            if (m_cancel)
                emit canceled();
            else
                emit failed(classifyError(error));
            return;
        }
        // 到文件末尾解出来的会比预期短；不是到区间末尾时在安静处切开
        const double decoded = pcm.size() / double(sr);
        double cut = toLimit || decoded < target ? decoded : MediaDecoder::quietestPoint(pcm, target - margin, target + margin);
        pcm.resize(size_t(cut * sr));
        const double coverEnd = (toLimit || decoded < target) ? limit : start + cut;

        auto node = segmenters.extract(key(start));
        IncrementalSegmenter seg = node.empty() ? IncrementalSegmenter(o.segmenter) : std::move(node.mapped());
        QList<Segment> recent = recentRaw.count(key(start)) ? recentRaw[key(start)] : QList<Segment>();
        recentRaw.erase(key(start));
        const double covered = coverage.coveredSeconds();
        QList<Segment> raw;
        if (cut >= 0.3) {
            raw = m_transcriber->transcribe(
                pcm, [&](double p) { report(Transcribing, (covered + p * (coverEnd - start)) / result.mediaDuration); }, &m_cancel,
                &error,
                [&](const QList<Segment>& fresh) {
                    // 去掉识别卡住时反复输出的同一句（和本区间之前的几段一起判断）
                    const QList<Segment> kept = dropRepeats(recent + fresh).mid(recent.size());
                    recent = (recent + fresh).mid(qMax(qsizetype(0), recent.size() + fresh.size() - 2));
                    add(seg.feed(kept));
                },
                start);
        }
        pcm.clear();
        if (m_cancel) {
            pool.waitForDone();
            emit canceled();
            return;
        }
        if (raw.isEmpty() && !error.isEmpty()) {
            pool.waitForDone();
            emit failed(classifyError(error));
            return;
        }
        result.rawSegments += int(raw.size());
        coverage.add(start, coverEnd);
        if (coverEnd >= limit - 0.05) {
            add(seg.finish()); // 接上了后面已识别的区间（或到了结尾）：这个区间结束
        } else {
            segmenters.emplace(key(coverEnd), std::move(seg));
            recentRaw[key(coverEnd)] = recent;
        }
        emit coverageChanged(coverage.ranges());
        submit(true); // 一段识别完：把这一段剩下的行也提交翻译
    }
    for (auto& [k, seg] : segmenters)
        add(seg.finish());
    result.covered = coverage.ranges();
    if (all.isEmpty()) {
        pool.waitForDone();
        emit failed(makeError(TaskError::Input, tr("没有识别到日语语音")));
        return;
    }
    result.transcribeSeconds = timer.restart() / 1000.0;

    // 4. 等剩下的翻译完成：失败不丢弃识别结果，交给界面提示和重试
    if (o.translate) {
        enter(Translating);
        submit(true);
        const int total = int(all.size());
        while (!pool.waitForDone(200)) {
            QMutexLocker lock(&mutex);
            report(Translating, double(zh.size()) / qMax(1, total));
        }
        if (m_cancel) {
            emit canceled();
            return;
        }
        for (auto it = zh.cbegin(); it != zh.cend(); ++it)
            all[it.key()].zh = it.value();
        result.translateSeconds = timer.restart() / 1000.0;
    }

    // 按时间排序作为最终结果（id 不变，界面按 id 对应）
    QList<Segment> lines;
    for (int id : sortedIds())
        lines.append(all[id]);

    if (o.translate) {
        // 5. 全片校对：统一专有名词、纠正识别错误（失败不影响已有译文）
        if (o.review && !result.translateError.isError()) {
            enter(Reviewing);
            result.review = translator.review(&lines, &m_cancel);
            if (m_cancel) {
                emit canceled();
                return;
            }
            result.reviewSeconds = timer.restart() / 1000.0;
        }
        result.usage = translator.usage();
        for (const Segment& s : lines)
            result.untranslated += s.zh.trimmed().isEmpty() ? 1 : 0;
    } else {
        result.untranslated = int(lines.size());
    }
    result.segments = lines;
    emit segmentsUpdated(lines);
    emit progressChanged(1.0);
    emit finished(result);
}
