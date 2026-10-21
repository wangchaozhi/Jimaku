#pragma once

#include "core/pipeline.h"

#include <QMainWindow>

class DropPage;
class EditorPage;
class ProgressPage;
class QStackedWidget;

// 三页：拖放 → 处理中 → 编辑/导出
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void chooseFile();
    void startFile(const QString& path);
    bool ensureModel();
    bool ensureApiKey();
    void openSettings();
    void retranslate();

    Pipeline* m_pipeline;
    QStackedWidget* m_pages;
    DropPage* m_drop;
    ProgressPage* m_progress;
    EditorPage* m_editor;
    QString m_currentFile;
    bool m_retranslating = false;
};
