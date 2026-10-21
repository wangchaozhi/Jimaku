#pragma once

#include "core/pipeline.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTableView;
class SubtitleModel;

// 结果页：双语字幕编辑表 + 导出 / 烧录。翻译有缺失时顶部常驻提示，可一键补译
class EditorPage : public QWidget {
    Q_OBJECT
public:
    explicit EditorPage(QWidget* parent = nullptr);

    void setResult(const QString& videoPath, const PipelineResult& result);
    QList<Segment> segments() const;
    void setRetranslating(bool on);

signals:
    void retranslateRequested();
    void newVideoRequested();
    void settingsRequested();

private:
    void updateBanner();
    void exportSubtitles();
    void burnIn();

    QString m_video;
    double m_duration = 0;
    TaskError m_translateError;
    QString m_lastTranslateError;

    QLabel* m_title;
    QWidget* m_banner;
    QLabel* m_bannerText;
    QPushButton* m_retry;
    QTableView* m_table;
    SubtitleModel* m_model;
};
