#include "modelmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>

namespace {

const QString kWhisperBase = QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/");

} // namespace

ModelManager::ModelManager(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

const QList<ModelInfo>& ModelManager::models()
{
    static const QList<ModelInfo> list = {
        {QStringLiteral("large-v3-turbo"), tr("高精度（large-v3-turbo，推荐）"), QStringLiteral("ggml-large-v3-turbo.bin"), 1624555275},
        {QStringLiteral("medium"), tr("均衡（medium）"), QStringLiteral("ggml-medium.bin"), 1533763059},
        {QStringLiteral("small"), tr("快速（small）"), QStringLiteral("ggml-small.bin"), 487601967},
    };
    return list;
}

QString ModelManager::defaultModelId()
{
    return models().first().id;
}

ModelInfo ModelManager::find(const QString& id)
{
    for (const ModelInfo& m : models()) {
        if (m.id == id)
            return m;
    }
    return models().first();
}

QString ModelManager::modelsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/models");
}

QString ModelManager::modelPath(const QString& id)
{
    return modelsDir() + QLatin1Char('/') + find(id).fileName;
}

bool ModelManager::isDownloaded(const QString& id)
{
    return QFileInfo(modelPath(id)).size() > 1024 * 1024;
}

void ModelManager::download(const QString& id)
{
    if (isBusy())
        return;
    m_canceled = false;
    if (isDownloaded(id)) {
        emit finished(true, QString());
        return;
    }
    const QUrl url(kWhisperBase + find(id).fileName);
    const QString target = modelPath(id);
    QDir().mkpath(modelsDir());
    m_file.setFileName(target + QStringLiteral(".part"));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit finished(false, tr("无法写入 %1：%2").arg(m_file.fileName(), m_file.errorString()));
        return;
    }
    m_timer.start();
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] { m_file.write(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const double secs = m_timer.elapsed() / 1000.0;
        emit progress(received, total, secs > 0 ? received / secs : 0);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, target] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        m_file.write(reply->readAll());
        m_file.close();
        if (m_canceled || reply->error() != QNetworkReply::NoError) {
            m_file.remove();
            emit finished(false, m_canceled ? QString() : tr("模型下载失败：%1").arg(reply->errorString()));
            return;
        }
        QFile::remove(target);
        if (!m_file.rename(target)) {
            emit finished(false, tr("无法保存模型文件：%1").arg(target));
            return;
        }
        emit finished(true, QString());
    });
}

void ModelManager::cancel()
{
    if (m_reply) {
        m_canceled = true;
        m_reply->abort();
    }
}
