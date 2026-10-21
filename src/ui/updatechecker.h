#pragma once

#include <QObject>

class QNetworkAccessManager;

// 从 GitHub Releases 检查新版本（只提示，不自动下载安装）
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // quiet：启动时自动检查，一天最多一次，出错不提示
    void check(bool quiet);

signals:
    void updateAvailable(const QString& version, const QString& url);
    void upToDate();
    void failed(const QString& error);

private:
    QNetworkAccessManager* m_nam;
};
