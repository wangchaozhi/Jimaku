#pragma once

#include "segment.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <atomic>
#include <functional>
#include <vector>

struct whisper_context;

// whisper.cpp 日语识别。模型加载一次后留在内存里，同一进程处理下一个文件时直接复用。
// 固定 language="ja"、不用 whisper 自带的 translate（翻译交给 Translator）
class Transcriber {
public:
    Transcriber() = default;
    ~Transcriber();
    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;

    // 模型路径与已加载的相同则什么都不做
    bool load(const QString& modelPath, QString* error);
    bool isLoaded() const { return m_ctx != nullptr; }
    QString modelPath() const { return m_modelPath; }

    // 不用 VAD：实测切块后丢失上下文，识别反而更差、还会出现幻觉。
    // 返回 whisper 的原始分段，每段带词级时间戳；失败返回空并写 error（取消时 error 为空）。
    // onSegments 在识别过程中（工作线程里）每出几段调用一次，用于边识别边显示
    QList<Segment> transcribe(const std::vector<float>& pcm,
                              const std::function<void(double)>& progress, const std::atomic_bool* cancel,
                              QString* error, const std::function<void(const QList<Segment>&)>& onSegments = {},
                              double timeOffset = 0); // 分段识别时这一段在整个视频里的起点（秒），加到所有时间上

private:
    whisper_context* m_ctx = nullptr;
    QString m_modelPath;
};

// whisper 的 token 是字节级的，一个日文字符可能被拆在两个 token 里。
// 返回 bytes 中"以完整 UTF-8 字符结尾"的最长前缀长度
int completeUtf8Length(const QByteArray& bytes);
