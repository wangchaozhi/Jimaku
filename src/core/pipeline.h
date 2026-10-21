#pragma once

#include "coverage.h"
#include "errors.h"
#include "segmenter.h"
#include "transcriber.h"
#include "translator.h"

#include <QObject>
#include <QPointer>
#include <QThread>
#include <atomic>
#include <memory>

struct PipelineOptions {
    QString input;
    QString modelPath;
    bool translate = true;
    bool review = true; // 翻译完成后全片校对：统一专有名词、纠正识别错误
    TranslatorConfig translator;
    SegmenterOptions segmenter;
    // 长视频分段识别：第一段短一些，第一行字幕更快出现
    double firstChunk = 60;
    double chunkLength = 300;
    Transcriber::Decoding decoding = Transcriber::Decoding::Greedy;
    // 断点续做：上次已经识别的区间和字幕（按时间排序，id 按这里的顺序重新编号）
    QList<TimeRange> resumeCovered;
    QList<Segment> resumeSegments;
};

struct PipelineResult {
    QList<Segment> segments;
    int rawSegments = 0;     // whisper 原始分段数（断句前）
    int untranslated = 0;    // 没有译文的行数；> 0 时界面必须提示
    TaskError translateError; // 翻译整体失败（key 错、连不上）；识别结果仍然保留
    QString lastTranslateError;
    ReviewOutcome review;
    TranslateUsage usage;
    double mediaDuration = 0;
    QList<TimeRange> covered;
    bool resumed = false;     // 接着上次的进度做的
    // translateSeconds：识别结束后还要等翻译多久（翻译和识别是同时进行的）
    double decodeSeconds = 0, loadSeconds = 0, transcribeSeconds = 0, translateSeconds = 0, reviewSeconds = 0;
    bool modelReused = false; // 模型已经在内存里，没有重新加载
};

// 解码 → 加载模型 → 识别 → 断句 → 翻译，在工作线程里执行；信号回到主线程
class Pipeline : public QObject {
    Q_OBJECT
public:
    enum Stage { Decoding, LoadingModel, Transcribing, Translating, Reviewing };
    Q_ENUM(Stage)

    explicit Pipeline(QObject* parent = nullptr);
    ~Pipeline() override;

    void start(const PipelineOptions& options);
    // 只补译 zh 为空的行（"重试翻译"）
    void retranslate(const QList<Segment>& segments, const TranslatorConfig& config);
    void cancel();
    // 优先识别这个时刻附近（预览时跳到了还没识别的地方）。线程安全
    void prioritize(double seconds);
    bool isRunning() const { return m_thread && m_thread->isRunning(); }

    static QString stageName(Stage stage);

signals:
    void stageChanged(Pipeline::Stage stage);
    void progressChanged(double fraction); // 整体进度 0..1
    // 边处理边推送（工作线程发出）。分段识别时行不一定按时间顺序到达，用 id 对应
    void linesAdded(const QList<Segment>& lines);
    void translated(const QList<int>& ids, const QStringList& zh);
    void coverageChanged(const QList<TimeRange>& ranges); // 已识别的区间
    void segmentsUpdated(const QList<Segment>& segments); // 最终结果（按时间排序，含校对修改）
    void finished(const PipelineResult& result);
    void failed(const TaskError& error);
    void canceled();

private:
    void runAll(const PipelineOptions& options);
    void launch(std::function<void()> work);

    std::unique_ptr<Transcriber> m_transcriber; // 跨任务保留，模型只加载一次
    QPointer<QThread> m_thread;
    std::atomic_bool m_cancel{false};
    std::atomic<double> m_priority{-1};
};
