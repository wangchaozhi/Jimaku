#pragma once

#include <QObject>
#include <QProcess>

// 用 ffmpeg 把 ASS 字幕烧进视频（硬字幕），异步执行，按输出里的 time= 报告进度
class Burner : public QObject {
    Q_OBJECT
public:
    explicit Burner(QObject* parent = nullptr);

    void start(const QString& video, const QString& assFile, const QString& output, double duration);
    void cancel();

    // ffmpeg subtitles 滤镜的路径要转义 \ : '（Windows 盘符的冒号也要）
    static QString escapeFilterPath(const QString& path);

signals:
    void progress(double fraction);
    void finished(bool ok, const QString& error);

private:
    QProcess m_process;
    QByteArray m_log;
    double m_duration = 0;
    bool m_canceled = false;
};
