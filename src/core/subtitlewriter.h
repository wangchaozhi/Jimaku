#pragma once

#include "segment.h"

// 导出字幕文件
namespace SubtitleWriter {

enum class Layout { Bilingual, ChineseOnly, JapaneseOnly }; // 双语时中文在上、日文在下
enum class Format { Srt, Ass };

// 字幕外观（预览、ASS 导出和烧录用；SRT 没有样式，由播放器决定）
struct Appearance {
    bool top = false;   // 放在画面顶部（视频自带的字幕在底部时避开）
    double scale = 1.0; // 字号倍数
};

QString toSrt(const QList<Segment>& segments, Layout layout);
QString toAss(const QList<Segment>& segments, Layout layout, const Appearance& appearance = {});
bool write(const QList<Segment>& segments, const QString& path, Format format, Layout layout, QString* error,
           const Appearance& appearance = {});

QString formatSrtTime(double seconds); // 00:01:02,345
QString formatAssTime(double seconds); // 0:01:02.35

} // namespace SubtitleWriter
