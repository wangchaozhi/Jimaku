#pragma once

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
    TranslatorConfig translator;
    SegmenterOptions segmenter;
};

struct PipelineResult {
    QList<Segment> segments;
    int rawSegments = 0;     // whisper 原始分段数（断句前）
    int untranslated = 0;    // 没有译文的行数；> 0 时界面必须提示
    TaskError translateError; // 翻译整体失败（key 错、连不上）；识别结果仍然保留
    QString lastTranslateError;
    double mediaDuration = 0;
    // translateSeconds：识别结束后还要等翻译多久（翻译和识别是同时进行的）
    double decodeSeconds = 0, loadSeconds = 0, transcribeSeconds = 0, translateSeconds = 0;
    bool modelReused = false; // 模型已经在内存里，没有重新加载
};

// 解码 → 加载模型 → 识别 → 断句 → 翻译，在工作线程里执行；信号回到主线程
class Pipeline : public QObject {
    Q_OBJECT
public:
    enum Stage { Decoding, LoadingModel, Transcribing, Translating };
    Q_ENUM(Stage)

    explicit Pipeline(QObject* parent = nullptr);
    ~Pipeline() override;

    void start(const PipelineOptions& options);
    // 只补译 zh 为空的行（"重试翻译"）
    void retranslate(const QList<Segment>& segments, const TranslatorConfig& config);
    void cancel();
    bool isRunning() const { return m_thread && m_thread->isRunning(); }

    static QString stageName(Stage stage);

signals:
    void stageChanged(Pipeline::Stage stage);
    void progressChanged(double fraction); // 整体进度 0..1
    // 边处理边推送（工作线程发出）：新定稿的字幕行、某一批的译文、已识别到的时刻（秒）
    void linesAdded(int firstIndex, const QList<Segment>& lines);
    void translated(int firstIndex, const QStringList& zh);
    void transcribedUntil(double seconds);
    void finished(const PipelineResult& result);
    void failed(const TaskError& error);
    void canceled();

private:
    void runAll(const PipelineOptions& options);
    void launch(std::function<void()> work);

    std::unique_ptr<Transcriber> m_transcriber; // 跨任务保留，模型只加载一次
    QPointer<QThread> m_thread;
    std::atomic_bool m_cancel{false};
};
