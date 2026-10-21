#pragma once

#include "segment.h"

// 读入已有的字幕文件（导入后可以翻译、修改、导出、烧录）
namespace SubtitleReader {

// 解析 SRT：每条的文字（多行合成一行）放进 ja；时间解析失败的条目跳过。结果按开始时间排序
QList<Segment> parseSrt(const QString& text);
bool readFile(const QString& path, QList<Segment>* segments, QString* error);

// 找和字幕同名的视频：video.mp4 对应 video.srt、video.ja.srt、video.zh.srt…；找不到返回空
QString findVideoFor(const QString& subtitlePath);

} // namespace SubtitleReader
