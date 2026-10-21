#pragma once

#include "coverage.h"
#include "segment.h"

#include <QDateTime>
#include <QString>

// 每个视频的处理进度和字幕（含手工修改）自动保存在 AppDataLocation/projects 里。
// 中途退出、崩溃后再次打开同一个视频，从中断处继续；处理完的直接打开结果
struct Project {
    QString video;          // 绝对路径
    qint64 size = 0;        // 文件大小 + 修改时间：视频换了就不再沿用
    QDateTime modified;
    QString model;          // 识别模型；换了模型也不沿用
    double duration = 0;
    QList<TimeRange> covered;   // 已识别的区间
    QList<Segment> segments;    // 按时间排序
    bool complete = false;      // 识别、翻译、校对都做完了

    bool isEmpty() const { return segments.isEmpty() && covered.isEmpty(); }

    // 找到这个视频（同一文件、同一模型）上次的进度；没有返回空的 Project
    static Project load(const QString& video, const QString& model);
    bool save(QString* error = nullptr) const;
    static void remove(const QString& video);

    static QString fileFor(const QString& video); // 便于测试和排查
};

// 读回进度时整理字幕：按时间排序、去掉完全相同的重复行；没做完的进度还要去掉不在已识别区间里的行
// （中断时正在识别的那一段已经推送出来的行——续做时这一段会重新识别，留着就会重复）
QList<Segment> sanitizeSegments(QList<Segment> segments, const QList<TimeRange>& covered, bool complete);
