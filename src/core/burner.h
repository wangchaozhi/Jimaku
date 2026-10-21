#pragma once

#include <QObject>
#include <QProcess>

// 用 ffmpeg 输出带字幕的视频，异步执行，按输出里的 time= 报告进度。
// 硬字幕（烧进画面）需要 ffmpeg 带 libass；没有时可以退回软字幕（嵌入可开关的字幕轨，不重新编码）
class Burner : public QObject {
    Q_OBJECT
public:
    explicit Burner(QObject* parent = nullptr);

    // 当前 ffmpeg 是否有 subtitles 滤镜（Homebrew 默认的 ffmpeg 没有 libass）
    static bool supportsHardSubtitles();

    void start(const QString& video, const QString& assFile, const QString& output, double duration);
    // 软字幕：mp4 里加一条 mov_text 字幕轨
    void startSoft(const QString& video, const QString& srtFile, const QString& output, double duration);
    // 不重新编码，把视频换成 MP4 容器（系统播放器不支持 MKV 等格式时用来预览）
    void startRemux(const QString& video, const QString& output, double duration);
    void cancel();

    // 视频编码参数：macOS 有硬件编码器时用 VideoToolbox（码率同原视频），否则 x264 veryfast
    static QStringList videoEncoderArgs(const QString& video);

    // ffmpeg subtitles 滤镜的路径要转义 \ : '（Windows 盘符的冒号也要）
    static QString escapeFilterPath(const QString& path);

signals:
    void progress(double fraction);
    void finished(bool ok, const QString& error);

private:
    void run(const QStringList& args, double duration);

    QProcess m_process;
    QByteArray m_log;
    double m_duration = 0;
    bool m_canceled = false;
};
