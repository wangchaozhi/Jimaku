#pragma once

#include "core/pipeline.h"

#include <QWidget>

class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QTableView;
class SubtitleModel;
class Timeline;
class VideoView;

// 工作台：视频预览 + 字幕表。处理过程中字幕逐行出现、译文随后补上，
// 播放到哪一段画面上就显示那一段的字幕；处理完成后可以编辑、导出、烧录
class WorkspacePage : public QWidget {
    Q_OBJECT
public:
    explicit WorkspacePage(QWidget* parent = nullptr);

    // 开始处理一个新视频：加载预览、清空字幕
    void begin(const QString& videoPath);
    void setStage(const QString& text);
    void addLines(int firstIndex, const QList<Segment>& lines);
    void addTranslations(int firstIndex, const QStringList& zh);
    void setTranscribedUntil(double seconds);
    void updateSegments(const QList<Segment>& segments); // 校对后的整表
    void finish(const PipelineResult& result);
    void fail(const TaskError& error);
    // "重试翻译"的结果：整表替换
    void setRetranslated(const PipelineResult& result);
    void setRetranslating(bool on);
    void stopPlayback();

    QList<Segment> segments() const;

signals:
    void cancelRequested();
    void retranslateRequested();
    void newVideoRequested();
    void settingsRequested();

private:
    void setProcessing(bool on);
    void refreshOverlay();
    void refreshStatus();
    void updateBanner();
    void exportSubtitles();
    void burnIn();

    QString m_video;
    double m_duration = 0;
    bool m_processing = false;
    double m_transcribed = 0;  // 已识别到的秒数
    int m_translated = 0;      // 已拿到译文结果的行数（含失败）
    QString m_stage;
    TaskError m_failure;       // 识别阶段的失败
    TaskError m_translateError;
    QString m_lastTranslateError;
    QString m_summary; // 完成后的统计（术语、纠错、用量），显示在顶栏

    QLabel* m_title;
    QLabel* m_status;
    QPushButton* m_cancel;
    QWidget* m_banner;
    QLabel* m_bannerText;
    QPushButton* m_retry;
    VideoView* m_view;
    QMediaPlayer* m_player;
    QAudioOutput* m_audio;
    QPushButton* m_play;
    Timeline* m_timeline;
    QLabel* m_time;
    QTableView* m_table;
    SubtitleModel* m_model;
    QPushButton* m_newVideo;
    QPushButton* m_burn;
    QPushButton* m_export;
};
