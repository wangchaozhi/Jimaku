#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

// whisper.cpp 的 ggml 模型：存放在 AppDataLocation/models，首次使用时下载
struct ModelInfo {
    QString id;        // large-v3-turbo / medium / small
    QString label;     // 界面显示
    QString fileName;  // ggml-large-v3-turbo.bin
    qint64 approxSize; // 字节，用于提示
};

class ModelManager : public QObject {
    Q_OBJECT
public:
    explicit ModelManager(QObject* parent = nullptr);

    static const QList<ModelInfo>& models();
    static ModelInfo find(const QString& id); // 找不到时返回默认模型
    static QString defaultModelId();
    static QString modelsDir();
    static QString modelPath(const QString& id);
    static bool isDownloaded(const QString& id);
    // 下载识别模型。同一时间只下载一个
    void download(const QString& id);
    void cancel();
    bool isBusy() const { return m_reply != nullptr; }

signals:
    void progress(qint64 received, qint64 total, double bytesPerSecond);
    void finished(bool ok, const QString& error);

private:
    QNetworkAccessManager* m_nam;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QElapsedTimer m_timer;
    bool m_canceled = false;
};
