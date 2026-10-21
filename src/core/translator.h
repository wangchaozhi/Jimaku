#pragma once

#include "errors.h"
#include "segment.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QPair>
#include <QStringList>
#include <atomic>
#include <functional>

// 调 OpenAI 兼容的 Chat Completions 接口（DeepSeek / OpenAI / Ollama / 自定义）把日文字幕译成简体中文。
// 同步阻塞，放在工作线程里调用
struct TranslatorConfig {
    QString baseUrl;   // 例如 https://api.deepseek.com/v1
    QString apiKey;    // Ollama 可为空
    QString model;
    QList<QPair<QString, QString>> glossary; // 日文 → 中文，要求译文必须使用
    int batchSize = 20;   // 边识别边翻译时用更小的批（见 Pipeline）
    int contextLines = 3; // 每批前后各附几句原文帮助理解（不翻译）
    int parallel = 4;     // 同时进行的请求数
    int maxRetries = 3;
    int timeoutMs = 120000;
};

struct TranslateOutcome {
    int failed = 0;   // 最终仍没有译文的行数
    TaskError fatal;  // 鉴权失败、连不上服务等：后面的行不再尝试
    QString lastError;
};

class Translator {
    Q_DECLARE_TR_FUNCTIONS(Translator)
public:
    explicit Translator(TranslatorConfig config);

    // 只翻译 zh 为空的行；progress 参数 0..1
    TranslateOutcome translate(QList<Segment>* segments, const std::function<void(double)>& progress,
                               const std::atomic_bool* cancel);

    // 翻译一批（失败或返回空译文的行逐条兜底）。zh 与 lines 等长，没翻出来的位置为空。线程安全
    struct BatchOutcome {
        QStringList zh;
        bool fatal = false;    // key 错、连不上：不要再发后续请求
        bool canceled = false;
        QString error;         // 最后一个错误（有行没翻出来时非空）
    };
    BatchOutcome translateLines(const QStringList& lines, const QStringList& before, const QStringList& after,
                                const std::atomic_bool* cancel);

    // 解析模型回复：{"t":[...]}，或直接是字符串数组；容忍 ``` 代码块和前后多余文字。
    // 条数不等于 expected 时返回空
    static QStringList parseReply(const QString& content, int expected);

    // 一批请求的 messages（单独暴露出来便于测试提示词）
    QJsonArray buildMessages(const QStringList& lines, const QStringList& before, const QStringList& after) const;

private:
    enum class Result { Ok, Retryable, Fatal, Canceled };
    Result post(const QJsonArray& messages, QString* content, QString* error, const std::atomic_bool* cancel);
    Result translateBatch(const QStringList& lines, const QStringList& before, const QStringList& after,
                          QStringList* out, QString* error, const std::atomic_bool* cancel);

    TranslatorConfig m_config;
};
