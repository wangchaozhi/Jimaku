#pragma once

#include "core/errors.h"

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;

// 处理中：文件名、阶段、进度条、取消；失败时同一页换成错误说明
class ProgressPage : public QWidget {
    Q_OBJECT
public:
    explicit ProgressPage(QWidget* parent = nullptr);

    void showRunning(const QString& fileName);
    void setStage(const QString& text);
    void setProgress(double fraction);
    void showError(const TaskError& error);

signals:
    void cancelRequested();
    void backRequested();

private:
    QStackedWidget* m_stack;
    QLabel* m_file;
    QLabel* m_stage;
    QProgressBar* m_bar;
    QPushButton* m_cancel;
    QLabel* m_errorText;
    QLabel* m_errorHint;
};
