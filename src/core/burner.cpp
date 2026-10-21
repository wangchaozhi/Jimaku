#include "burner.h"

#include "mediadecoder.h"

#include <QDir>
#include <QRegularExpression>

Burner::Burner(QObject* parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, [this] {
        const QByteArray chunk = m_process.readAll();
        m_log += chunk;
        if (m_log.size() > 8192)
            m_log = m_log.right(8192);
        static const QRegularExpression re(QStringLiteral("time=(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)"));
        auto it = re.globalMatch(QString::fromUtf8(chunk));
        double t = -1;
        while (it.hasNext()) {
            const auto m = it.next();
            t = m.captured(1).toInt() * 3600 + m.captured(2).toInt() * 60 + m.captured(3).toDouble();
        }
        if (t >= 0 && m_duration > 0)
            emit progress(qMin(1.0, t / m_duration));
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (m_canceled) {
            emit finished(false, QString());
        } else if (status != QProcess::NormalExit || code != 0) {
            const QString log = QString::fromUtf8(m_log).trimmed();
            emit finished(false, tr("ffmpeg 烧录失败：%1").arg(log.section(QLatin1Char('\n'), -3)));
        } else {
            emit finished(true, QString());
        }
    });
}

QString Burner::escapeFilterPath(const QString& path)
{
    QString p = QDir::fromNativeSeparators(path);
    p.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    p.replace(QLatin1Char(':'), QStringLiteral("\\:"));
    p.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    return p;
}

void Burner::start(const QString& video, const QString& assFile, const QString& output, double duration)
{
    m_duration = duration;
    m_canceled = false;
    m_log.clear();
    const QString ffmpeg = MediaDecoder::findTool(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        emit finished(false, tr("找不到 ffmpeg"));
        return;
    }
    m_process.start(ffmpeg, {QStringLiteral("-nostdin"), QStringLiteral("-y"), QStringLiteral("-i"), video,
                             QStringLiteral("-vf"), QStringLiteral("subtitles='%1'").arg(escapeFilterPath(assFile)),
                             QStringLiteral("-c:a"), QStringLiteral("copy"), output});
}

void Burner::cancel()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_canceled = true;
        m_process.kill();
    }
}
