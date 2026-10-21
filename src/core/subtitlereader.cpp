#include "subtitlereader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <algorithm>

namespace SubtitleReader {

QList<Segment> parseSrt(const QString& text)
{
    static const QRegularExpression timing(
        QStringLiteral("(\\d+):(\\d+):(\\d+)[,.](\\d+)\\s*-->\\s*(\\d+):(\\d+):(\\d+)[,.](\\d+)"));
    auto seconds = [](const QRegularExpressionMatch& m, int i) {
        return m.captured(i).toInt() * 3600 + m.captured(i + 1).toInt() * 60 + m.captured(i + 2).toInt()
               + m.captured(i + 3).leftJustified(3, QLatin1Char('0')).left(3).toInt() / 1000.0;
    };
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (normalized.startsWith(QChar(0xFEFF)))
        normalized.remove(0, 1);
    QList<Segment> out;
    for (const QString& block : normalized.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts)) {
        const QStringList lines = block.split(QLatin1Char('\n'));
        int t = -1;
        for (int i = 0; i < lines.size() && i < 2; ++i) {
            if (timing.match(lines[i]).hasMatch()) {
                t = i;
                break;
            }
        }
        if (t < 0)
            continue;
        const auto m = timing.match(lines[t]);
        Segment s;
        s.start = seconds(m, 1);
        s.end = seconds(m, 5);
        QStringList body;
        for (int i = t + 1; i < lines.size(); ++i) {
            QString l = lines[i].trimmed();
            l.remove(QRegularExpression(QStringLiteral("<[^>]+>"))); // <i> <font> 等标签
            if (!l.isEmpty())
                body << l;
        }
        s.ja = body.join(QLatin1Char(' '));
        if (!s.ja.isEmpty() && s.end > s.start)
            out.append(s);
    }
    std::stable_sort(out.begin(), out.end(), [](const Segment& a, const Segment& b) { return a.start < b.start; });
    return out;
}

bool readFile(const QString& path, QList<Segment>* segments, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("无法读取 %1：%2").arg(path, f.errorString());
        return false;
    }
    *segments = parseSrt(QString::fromUtf8(f.readAll()));
    if (segments->isEmpty()) {
        *error = QObject::tr("没有读到字幕（目前支持 UTF-8 编码的 SRT）：%1").arg(path);
        return false;
    }
    return true;
}

QString findVideoFor(const QString& subtitlePath)
{
    static const QStringList videoExts = {QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("mov"), QStringLiteral("m4v"),
                                          QStringLiteral("webm"), QStringLiteral("avi"), QStringLiteral("ts"), QStringLiteral("flv")};
    const QFileInfo fi(subtitlePath);
    QString base = fi.completeBaseName(); // video.ja.srt → video.ja
    while (!base.isEmpty()) {
        for (const QString& ext : videoExts) {
            const QString candidate = fi.absoluteDir().filePath(base + QLatin1Char('.') + ext);
            if (QFileInfo::exists(candidate))
                return candidate;
        }
        const int dot = base.lastIndexOf(QLatin1Char('.'));
        if (dot <= 0)
            break;
        base.truncate(dot); // 去掉 .ja / .zh 这类语言后缀再找
    }
    return QString();
}

} // namespace SubtitleReader
