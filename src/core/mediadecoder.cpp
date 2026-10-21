#include "mediadecoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <cstring>

namespace MediaDecoder {

QString findTool(const QString& name)
{
    QStringList dirs{QCoreApplication::applicationDirPath()};
#ifdef Q_OS_MACOS
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/bin");
#endif
    QString found = QStandardPaths::findExecutable(name, dirs);
    if (found.isEmpty())
        found = QStandardPaths::findExecutable(name);
    if (found.isEmpty()) // 从 Finder 启动时 PATH 里没有 Homebrew
        found = QStandardPaths::findExecutable(name, {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")});
    return found;
}

double probeDuration(const QString& path, QString* error)
{
    if (!QFileInfo::exists(path)) {
        *error = QObject::tr("文件不存在：%1").arg(path);
        return -1;
    }
    const QString ffprobe = findTool(QStringLiteral("ffprobe"));
    if (ffprobe.isEmpty()) {
        *error = QObject::tr("找不到 ffprobe");
        return -1;
    }
    QProcess p;
    p.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                      QStringLiteral("format=duration"), QStringLiteral("-of"), QStringLiteral("csv=p=0"), path});
    if (!p.waitForFinished(30000) || p.exitCode() != 0) {
        *error = QObject::tr("ffprobe 读取失败：%1").arg(QString::fromUtf8(p.readAllStandardError()).trimmed());
        return -1;
    }
    bool ok = false;
    const double d = QString::fromUtf8(p.readAllStandardOutput()).trimmed().toDouble(&ok);
    if (!ok || d <= 0) {
        *error = QObject::tr("无法读取媒体时长（文件可能已损坏）：%1").arg(path);
        return -1;
    }
    return d;
}

bool decodePcm(const QString& path, double duration, std::vector<float>* pcm,
               const std::function<void(double)>& progress, const std::atomic_bool* cancel, QString* error)
{
    const QString ffmpeg = findTool(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        *error = QObject::tr("找不到 ffmpeg");
        return false;
    }
    QProcess p;
    p.start(ffmpeg, {QStringLiteral("-nostdin"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path,
                     QStringLiteral("-vn"), QStringLiteral("-ac"), QStringLiteral("1"), QStringLiteral("-ar"),
                     QString::number(kSampleRate), QStringLiteral("-f"), QStringLiteral("f32le"), QStringLiteral("-")});
    if (!p.waitForStarted()) {
        *error = QObject::tr("ffmpeg 启动失败：%1").arg(p.errorString());
        return false;
    }

    const double expectedBytes = duration * kSampleRate * sizeof(float);
    QByteArray bytes;
    bytes.reserve(qint64(expectedBytes) + 4096);
    while (p.state() != QProcess::NotRunning) {
        if (cancel && cancel->load()) {
            p.kill();
            p.waitForFinished();
            return false;
        }
        p.waitForReadyRead(200);
        bytes += p.readAllStandardOutput();
        if (progress && expectedBytes > 0)
            progress(qMin(1.0, bytes.size() / expectedBytes));
    }
    bytes += p.readAllStandardOutput();

    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        *error = QObject::tr("ffmpeg 解码失败：%1").arg(QString::fromUtf8(p.readAllStandardError()).trimmed());
        return false;
    }
    if (bytes.size() < int(sizeof(float)) * kSampleRate / 10) {
        *error = QObject::tr("没有解码出音频，文件可能不包含音轨");
        return false;
    }
    pcm->resize(bytes.size() / sizeof(float));
    std::memcpy(pcm->data(), bytes.constData(), pcm->size() * sizeof(float));
    if (progress)
        progress(1.0);
    return true;
}

} // namespace MediaDecoder
