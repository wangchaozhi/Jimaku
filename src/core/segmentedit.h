#pragma once

#include "segment.h"

#include <QPair>

// 字幕编辑操作（纯函数，界面和测试共用）
namespace SegmentEdit {

constexpr double kMinDuration = 0.2; // 一行至少显示的秒数

// 在 at 秒处把一行拆成两行：有词级时间时按词切文字，否则按时间比例切字符；译文按同样比例切。
// at 不在行内（离两端不到 kMinDuration）时返回 false
bool split(const Segment& s, double at, Segment* first, Segment* second);

// 合并相邻两行：时间取两行的开头和结尾，原文直接相连，译文用空格隔开
Segment merge(const Segment& a, const Segment& b);

// 改开始 / 结束时间：保证 start < end，至少 kMinDuration
void setStart(Segment* s, double start);
void setEnd(Segment* s, double end);

// 编辑后按开始时间重新排序（id 不变）
void sortByStart(QList<Segment>* segments);

} // namespace SegmentEdit
