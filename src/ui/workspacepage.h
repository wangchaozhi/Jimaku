#pragma once

#include "core/pipeline.h"
#include "core/project.h"

#include <QElapsedTimer>
#include <QTimer>
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

    // 开始处理一个视频：加载预览；resume 非空时先显示上次的进度（之后接着处理）
    void begin(const QString& videoPath, const QString& model, const Project& resume = {});
    // 直接打开上次处理完的结果（不再处理）
    void openResult(const Project& project);
    void setStage(const QString& text);
    void addLines(const QList<Segment>& lines);
    void addTranslations(const QList<int>& ids, const QStringList& zh);
    void setCoverage(const QList<TimeRange>& ranges);
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
    void priorityRequested(double seconds); // 预览跳到了还没识别的地方

private:
    void setProcessing(bool on);
    void refreshOverlay();
    void refreshStatus();
    void updateBanner();
    void exportSubtitles();
    void seekTo(qint64 ms); // 用户跳转：没识别到的地方请求优先处理
    void showFrameAt(qint64 ms); // 暂停状态下显示指定位置的画面
    void endPreview();
    bool covered(double seconds) const;
    void scheduleSave();
    void saveProject();
    void burnIn();

    QString m_video;
    double m_duration = 0;
    bool m_processing = false;
    QList<TimeRange> m_coverage; // 已识别的区间
    QString m_modelId;           // 识别模型（保存进度用）
    bool m_complete = false;     // 识别、翻译、校对都做完了
    QTimer m_saveTimer;          // 改动后 2 秒保存一次进度
    QElapsedTimer m_etaClock;    // 估算剩余时间：本次开始后识别了多少秒
    double m_etaBase = -1;
    bool m_previewing = false;   // 暂停时跳转：正在静音播放等新画面
    qint64 m_previewTarget = 0;
    int m_previewSerial = 0;
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
