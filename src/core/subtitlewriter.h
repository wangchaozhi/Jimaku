#pragma once

#include "segment.h"

// 导出字幕文件
namespace SubtitleWriter {

enum class Layout { Bilingual, ChineseOnly, JapaneseOnly }; // 双语时中文在上、日文在下
enum class Format { Srt, Ass };

QString toSrt(const QList<Segment>& segments, Layout layout);
QString toAss(const QList<Segment>& segments, Layout layout);
bool write(const QList<Segment>& segments, const QString& path, Format format, Layout layout, QString* error);

QString formatSrtTime(double seconds); // 00:01:02,345
QString formatAssTime(double seconds); // 0:01:02.35

} // namespace SubtitleWriter
