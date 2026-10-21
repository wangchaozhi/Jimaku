#pragma once

#include <QWidget>

class QLabel;

// 首页：一个大拖放区 + "选择视频"。拖放事件由 MainWindow 统一处理
class DropPage : public QWidget {
    Q_OBJECT
public:
    explicit DropPage(QWidget* parent = nullptr);

    void setHighlighted(bool on); // 拖着文件经过窗口时高亮
    void refreshSummary();        // 底部的"模型 · 翻译服务"一行
    void showUpdate(const QString& version, const QString& url);

signals:
    void chooseFileRequested();
    void settingsRequested();

private:
    QWidget* m_zone;
    QLabel* m_summary;
    QLabel* m_update;
};
