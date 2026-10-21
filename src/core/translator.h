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
// 同步阻塞，放在工作线程里调用；同一个 Translator 可以被多个线程同时使用
struct TranslatorConfig {
    QString baseUrl;   // 例如 https://api.deepseek.com/v1
    QString apiKey;    // Ollama 可为空
    QString model;
    QList<QPair<QString, QString>> glossary; // 日文 → 中文，要求译文必须使用
    bool subtitleStyle = true;     // 字幕风格：句末不加标点，句中逗号换成空格，译文尽量短
    bool translateLyrics = false;  // 英文歌词、口号也翻译（默认保留原文）
    QString cacheDir;              // 非空时缓存请求结果：同一个视频重新处理不再重复花钱
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

// 全片校对的结果（统一专有名词译法、纠正识别错误）
struct ReviewOutcome {
    QString summary;                          // 一句话概括视频内容
    QList<QPair<QString, QString>> terms;     // 识别出的专有名词：日文 → 中文
    int jaFixed = 0;                          // 纠正了识别错误的行数
    int zhFixed = 0;                          // 改了译文的行数
    QString error;                            // 校对失败时的原因（不影响已有译文）
};

struct TranslateUsage {
    qint64 promptTokens = 0;
    qint64 completionTokens = 0;
    int requests = 0;   // 实际发出的请求
    int cacheHits = 0;  // 命中缓存、没有发请求的次数
};

class Translator {
    Q_DECLARE_TR_FUNCTIONS(Translator)
public:
    explicit Translator(TranslatorConfig config);

    // 只翻译 zh 为空的行；progress 参数 0..1
    TranslateOutcome translate(QList<Segment>* segments, const std::function<void(double)>& progress,
                               const std::atomic_bool* cancel);

    // 翻译一批（失败或返回空译文的行逐条兜底）。zh 与 lines 等长，没翻出来的位置为空
    struct BatchOutcome {
        QStringList zh;
        bool fatal = false;    // key 错、连不上：不要再发后续请求
        bool canceled = false;
        QString error;         // 最后一个错误（有行没翻出来时非空）
    };
    BatchOutcome translateLines(const QStringList& lines, const QStringList& before, const QStringList& after,
                                const std::atomic_bool* cancel);

    // 全片校对：把所有日文和译文交给模型，统一人名等专有名词、结合上下文纠正识别错误，
    // 只改需要改的行（改过的行在 jaOriginal / zhOriginal 里保留原来的内容）。长视频分段进行，前面找到的术语带到后面
    ReviewOutcome review(QList<Segment>* segments, const std::atomic_bool* cancel);

    TranslateUsage usage() const;

    // —— 以下是纯函数，单独暴露便于测试 ——

    // 解析翻译回复：{"t":[...]}，或直接是字符串数组；容忍 ``` 代码块和前后多余文字。条数不等于 expected 时返回空
    static QStringList parseReply(const QString& content, int expected);

    // 行尾是句末标点（。！？…）：一句话到这里结束
    static bool endsSentence(const QString& ja);

    // 同一句话的连续几行编同一个号：行尾是句末标点（。！？…）才算一句结束
    static QList<int> sentenceGroups(const QStringList& lines);

    // 字幕风格：去掉句末的句号逗号，句中的逗号顿号换成空格（保留问号、感叹号、省略号）
    static QString applySubtitleStyle(const QString& zh);

    struct ReviewFix {
        int index = -1;
        QString ja; // 空 = 日文不用改
        QString zh; // 空 = 译文不用改
    };
    struct ParsedReview {
        bool ok = false;
        QString summary;
        QList<QPair<QString, QString>> terms;
        QList<ReviewFix> fixes;
    };
    // 解析校对回复；越界或空的修改被丢弃
    static ParsedReview parseReview(const QString& content, int firstIndex, int lineCount);

    // 一批翻译请求的 messages（单独暴露出来便于测试提示词）
    QJsonArray buildMessages(const QStringList& lines, const QStringList& before, const QStringList& after) const;

private:
    enum class Result { Ok, Retryable, Fatal, Canceled };
    Result post(const QJsonArray& messages, double temperature, QString* content, QString* error,
                const std::atomic_bool* cancel);
    Result translateBatch(const QStringList& lines, const QStringList& before, const QStringList& after,
                          QStringList* out, QString* error, const std::atomic_bool* cancel);
    QString finish(const QString& zh) const; // 套用字幕风格

    TranslatorConfig m_config;
    std::atomic<qint64> m_promptTokens{0};
    std::atomic<qint64> m_completionTokens{0};
    std::atomic_int m_requests{0};
    std::atomic_int m_cacheHits{0};
};
