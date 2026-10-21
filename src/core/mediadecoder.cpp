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

namespace {

// 运行 ffmpeg 把音频转成 16 kHz 单声道 float PCM，从标准输出读入
bool runDecoder(const QStringList& inputArgs, double duration, std::vector<float>* pcm,
                const std::function<void(double)>& progress, const std::atomic_bool* cancel, QString* error)
{
    const QString ffmpeg = findTool(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        *error = QObject::tr("找不到 ffmpeg");
        return false;
    }
    QProcess p;
    p.start(ffmpeg, QStringList{QStringLiteral("-nostdin"), QStringLiteral("-v"), QStringLiteral("error")} + inputArgs
                        + QStringList{QStringLiteral("-vn"), QStringLiteral("-ac"), QStringLiteral("1"), QStringLiteral("-ar"),
                                      QString::number(kSampleRate), QStringLiteral("-f"), QStringLiteral("f32le"), QStringLiteral("-")});
    if (!p.waitForStarted()) {
        *error = QObject::tr("ffmpeg 启动失败：%1").arg(p.errorString());
        return false;
    }
    const double expectedSamples = duration * kSampleRate;
    pcm->clear();
    pcm->reserve(size_t(qMax(0.0, expectedSamples)) + kSampleRate);
    QByteArray carry; // 读到的字节数不一定是 4 的倍数
    auto drain = [&] {
        carry += p.readAllStandardOutput();
        const qsizetype n = carry.size() / qsizetype(sizeof(float));
        if (n == 0)
            return;
        const size_t old = pcm->size();
        pcm->resize(old + size_t(n));
        std::memcpy(pcm->data() + old, carry.constData(), size_t(n) * sizeof(float));
        carry.remove(0, n * qsizetype(sizeof(float)));
    };
    while (p.state() != QProcess::NotRunning) {
        if (cancel && cancel->load()) {
            p.kill();
            p.waitForFinished();
            return false;
        }
        p.waitForReadyRead(200);
        drain();
        if (progress && expectedSamples > 0)
            progress(qMin(1.0, pcm->size() / expectedSamples));
    }
    drain();
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        *error = QObject::tr("ffmpeg 解码失败：%1").arg(QString::fromUtf8(p.readAllStandardError()).trimmed());
        return false;
    }
    if (progress)
        progress(1.0);
    return true;
}

} // namespace

bool decodeRange(const QString& path, double start, double length, std::vector<float>* pcm,
                 const std::atomic_bool* cancel, QString* error)
{
    // -ss 放在 -i 前面：直接跳到附近位置再解码，长视频的后半段也不用从头读
    return runDecoder({QStringLiteral("-ss"), QString::number(start, 'f', 3), QStringLiteral("-t"), QString::number(length, 'f', 3),
                       QStringLiteral("-i"), path},
                      length, pcm, {}, cancel, error);
}

double quietestPoint(const std::vector<float>& pcm, double from, double to)
{
    const int frame = kSampleRate / 20; // 50 ms
    const int window = 10;              // 0.5 s
    const qsizetype first = qMax<qsizetype>(0, qsizetype(from * kSampleRate) / frame);
    const qsizetype last = qMin<qsizetype>(qsizetype(pcm.size()) / frame, qsizetype(to * kSampleRate) / frame);
    if (last - first <= window)
        return qBound(0.0, (from + to) / 2, pcm.size() / double(kSampleRate));
    QList<double> energy;
    for (qsizetype f = first; f < last; ++f) {
        double e = 0;
        for (int i = 0; i < frame; ++i) {
            const float v = pcm[size_t(f * frame + i)];
            e += double(v) * v;
        }
        energy << e;
    }
    double sum = 0;
    for (int i = 0; i < window; ++i)
        sum += energy[i];
    double best = sum;
    qsizetype bestAt = 0;
    for (qsizetype i = window; i < energy.size(); ++i) {
        sum += energy[i] - energy[i - window];
        if (sum < best) {
            best = sum;
            bestAt = i - window + 1;
        }
    }
    return (first + bestAt + window / 2.0) * frame / double(kSampleRate);
}

bool decodePcm(const QString& path, double duration, std::vector<float>* pcm,
               const std::function<void(double)>& progress, const std::atomic_bool* cancel, QString* error)
{
    if (!runDecoder({QStringLiteral("-i"), path}, duration, pcm, progress, cancel, error))
        return false;
    if (pcm->size() < size_t(kSampleRate / 10)) {
        *error = QObject::tr("没有解码出音频，文件可能不包含音轨");
        return false;
    }
    return true;
}

} // namespace MediaDecoder
