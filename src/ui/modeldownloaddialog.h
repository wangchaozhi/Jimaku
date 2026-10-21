#pragma once

#include <QDialog>

class ModelManager;
class QLabel;
class QProgressBar;

// 下载识别模型：进度、速度、可取消。exec() 返回 Accepted 表示下载完成
class ModelDownloadDialog : public QDialog {
    Q_OBJECT
public:
    ModelDownloadDialog(const QString& modelId, QWidget* parent = nullptr);
    int exec() override;

private:
    QString m_modelId;
    ModelManager* m_manager;
    QLabel* m_text;
    QProgressBar* m_bar;
};
