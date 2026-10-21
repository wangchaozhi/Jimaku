#pragma once

#include "core/pipeline.h"

#include <QMainWindow>

class DropPage;
class QStackedWidget;
class WorkspacePage;

// 两页：拖放 → 工作台（边处理边预览、编辑、导出）
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openFile(const QString& path) { startFile(path); }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void chooseFile();
    void importSubtitles();
    void startFile(const QString& path);
    bool ensureModel();
    bool ensureApiKey();
    void openSettings();
    void retranslate();
    void showDropPage();

    Pipeline* m_pipeline;
    QStackedWidget* m_pages;
    DropPage* m_drop;
    WorkspacePage* m_workspace;
    QString m_currentFile;
    bool m_retranslating = false;
};
