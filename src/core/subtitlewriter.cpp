#include "subtitlewriter.h"

#include <QFile>
#include <QObject>
#include <QSaveFile>
#include <cmath>

namespace SubtitleWriter {

namespace {

// 中文为空（没翻出来）时退回日文，不输出空字幕
QStringList linesFor(const Segment& s, Layout layout)
{
    const QString ja = s.ja.trimmed();
    const QString zh = s.zh.trimmed();
    switch (layout) {
    case Layout::Bilingual:
        if (zh.isEmpty() || zh == ja)
            return {ja};
        return {zh, ja};
    case Layout::ChineseOnly:
        return {zh.isEmpty() ? ja : zh};
    case Layout::JapaneseOnly:
        return {ja};
    }
    return {ja};
}

qint64 toMs(double seconds)
{
    return qMax<qint64>(0, qint64(std::llround(seconds * 1000)));
}

} // namespace

QString formatSrtTime(double seconds)
{
    const qint64 ms = toMs(seconds);
    return QStringLiteral("%1:%2:%3,%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString formatAssTime(double seconds)
{
    const qint64 cs = (toMs(seconds) + 5) / 10;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(cs / 360000)
        .arg(cs / 6000 % 60, 2, 10, QLatin1Char('0'))
        .arg(cs / 100 % 60, 2, 10, QLatin1Char('0'))
        .arg(cs % 100, 2, 10, QLatin1Char('0'));
}

QString toSrt(const QList<Segment>& segments, Layout layout)
{
    QString out;
    int n = 0;
    for (const Segment& s : segments) {
        const QStringList lines = linesFor(s, layout);
        if (lines.join(QString()).trimmed().isEmpty())
            continue;
        out += QStringLiteral("%1\n%2 --> %3\n%4\n\n")
                   .arg(++n)
                   .arg(formatSrtTime(s.start), formatSrtTime(s.end), lines.join(QLatin1Char('\n')));
    }
    return out;
}

QString toAss(const QList<Segment>& segments, Layout layout, const Appearance& appearance)
{
    const int mainSize = int(std::lround(64 * appearance.scale));
    const int subSize = int(std::lround(44 * appearance.scale));
    const int alignment = appearance.top ? 8 : 2; // ASS 小键盘方位：8 顶部居中，2 底部居中
    // 中文用默认样式，双语时日文小一号放在下面
    QString out = QStringLiteral(
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "PlayResX: 1920\n"
        "PlayResY: 1080\n"
        "WrapStyle: 0\n"
        "ScaledBorderAndShadow: yes\n"
        "\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        "MarginR, MarginV, Encoding\n"
        "Style: Default,PingFang SC,%1,&H00FFFFFF,&H000000FF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,3,1,%2,60,60,50,1\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n").arg(mainSize).arg(alignment);
    for (const Segment& s : segments) {
        QStringList lines = linesFor(s, layout);
        for (QString& l : lines)
            l.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('{'), QLatin1Char('(')).replace(QLatin1Char('}'), QLatin1Char(')'));
        if (lines.join(QString()).trimmed().isEmpty())
            continue;
        QString text = lines.first();
        if (lines.size() > 1)
            text += QStringLiteral("\\N{\\fs%1}").arg(subSize) + lines.at(1);
        out += QStringLiteral("Dialogue: 0,%1,%2,Default,,0,0,0,,%3\n").arg(formatAssTime(s.start), formatAssTime(s.end), text);
    }
    return out;
}

bool write(const QList<Segment>& segments, const QString& path, Format format, Layout layout, QString* error,
           const Appearance& appearance)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = QObject::tr("无法写入 %1：%2").arg(path, f.errorString());
        return false;
    }
    const QString text = format == Format::Ass ? toAss(segments, layout, appearance) : toSrt(segments, layout);
    if (format == Format::Ass)
        f.write("\xEF\xBB\xBF"); // 部分播放器需要 BOM 才按 UTF-8 读 ASS
    f.write(text.toUtf8());
    if (!f.commit()) {
        *error = QObject::tr("无法写入 %1：%2").arg(path, f.errorString());
        return false;
    }
    return true;
}

} // namespace SubtitleWriter
