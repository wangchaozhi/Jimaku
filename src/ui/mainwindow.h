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
    // 批量：拖入或选中多个视频，依次处理（已处理过的跳过）
    void startFiles(const QStringList& paths);
    void startNextInQueue();
    bool ensureModel();
    bool ensureApiKey();
    void openSettings();
    void retranslate();
    void showDropPage();
    void prepareFurigana(); // 开着振假名：加载字典，没有就在后台下载

    Pipeline* m_pipeline;
    QStackedWidget* m_pages;
    DropPage* m_drop;
    WorkspacePage* m_workspace;
    QString m_currentFile;
    bool m_retranslating = false;
    QStringList m_queue;  // 批量处理中还没开始的视频
    bool m_batch = false;
    class DictionaryDownloader* m_dictDownload = nullptr;
};
