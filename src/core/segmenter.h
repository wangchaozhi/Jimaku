#pragma once

#include "segment.h"

// 按词级时间戳把 whisper 的原始分段重新切成适合阅读的字幕行（纯规则，不依赖大模型，结果可复现）
struct SegmenterOptions {
    int maxChars = 18;         // 单行最多字符（不计空白）
    double maxDuration = 6.0;  // 单行最长显示秒数
    double pauseSplit = 0.5;   // 词间停顿超过这个秒数就换行
    double minDuration = 1.0;  // 短于这个秒数的碎片尽量并到相邻行
};

QList<Segment> resegment(const QList<Segment>& raw, const SegmenterOptions& options = {});

// 边识别边断句：每收到 whisper 的几段原始分段就交出已经"定稿"的字幕行。
// 最后一行先留着（下一段可能要和它合并），所以交出的行之后不会再变，序号稳定
class IncrementalSegmenter {
public:
    explicit IncrementalSegmenter(const SegmenterOptions& options = {});

    QList<Segment> feed(const QList<Segment>& raw);
    QList<Segment> finish(); // 识别结束：交出留着的最后一行

private:
    SegmenterOptions m_options;
    QList<Segment> m_pending; // 0 或 1 行
};
