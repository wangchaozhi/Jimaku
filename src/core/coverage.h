#pragma once

#include <QList>
#include <QPair>

// 长视频分段识别：记录哪些时间区间已经识别过，决定下一段从哪里开始、最远到哪里
using TimeRange = QPair<double, double>; // [first, second) 秒

class Coverage {
public:
    explicit Coverage(double duration = 0);

    void add(double from, double to); // 自动合并相邻 / 重叠的区间
    bool covers(double t) const;
    bool complete() const;
    double duration() const { return m_duration; }
    double coveredSeconds() const;
    const QList<TimeRange>& ranges() const { return m_ranges; }

    // 下一段从哪里开始：priority（播放位置，<0 表示没有）落在没识别的地方就从那里开始
    // （离空隙起点不到 5 秒时从空隙起点开始，免得留下几秒的小缝）；否则从最早的空隙开始。全部识别完返回 -1
    double nextStart(double priority) const;
    // start 之后第一个已识别区间的起点；没有就是视频结尾。下一段不能越过这里
    double limitAfter(double start) const;

private:
    double m_duration;
    QList<TimeRange> m_ranges; // 按起点排序、互不重叠
};
