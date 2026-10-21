#include "check.h"
#include "core/subtitlereader.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

int main()
{
    // BOM、CRLF、多行文字、HTML 标签、乱序
    const QString srt = QStringLiteral("\xFEFF"
                                       "2\r\n00:00:05,500 --> 00:00:07,000\r\n<i>二つ目</i>\r\n\r\n"
                                       "1\r\n00:00:01,000 --> 00:00:03,250\r\nこんにちは\r\n世界\r\n\r\n"
                                       "坏条目\r\n没有时间轴\r\n\r\n"
                                       "3\r\n01:02:03.4 --> 01:02:05,000\r\n小数点写法\r\n");
    const QList<Segment> s = SubtitleReader::parseSrt(srt);
    expectEq("entries", int(s.size()), 3);
    expectEq("sorted first", s.value(0).ja, QStringLiteral("こんにちは 世界"));
    expectEq("tags stripped", s.value(1).ja, QStringLiteral("二つ目"));
    check("times", qAbs(s.value(0).start - 1.0) < 1e-6 && qAbs(s.value(0).end - 3.25) < 1e-6);
    check("dot millis", qAbs(s.value(2).start - 3723.4) < 1e-6);

    // 找同名视频：video.ja.srt → video.mp4
    QTemporaryDir dir;
    QFile(dir.filePath(QStringLiteral("movie.mp4"))).open(QIODevice::WriteOnly);
    expectEq("video for lang suffix", SubtitleReader::findVideoFor(dir.filePath(QStringLiteral("movie.ja.srt"))),
             QDir(dir.path()).filePath(QStringLiteral("movie.mp4")));
    check("no video", SubtitleReader::findVideoFor(dir.filePath(QStringLiteral("other.srt"))).isEmpty());

    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
