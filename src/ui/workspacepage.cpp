#include "workspacepage.h"

#include "core/burner.h"
#include "core/settings.h"
#include "core/subtitlewriter.h"
#include "subtitlemodel.h"
#include "timeline.h"
#include "videoview.h"

#include <QAudioOutput>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsVideoItem>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTableView>
#include <QTemporaryDir>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

QString clock(double seconds)
{
    const int s = int(qMax(0.0, seconds));
    return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

WorkspacePage::WorkspacePage(QWidget* parent)
    : QWidget(parent)
    , m_model(new SubtitleModel(this))
{
    // 顶栏：文件名、进度、取消、设置
    m_title = new QLabel(this);
    QFont f = m_title->font();
    f.setBold(true);
    m_title->setFont(f);
    m_status = new QLabel(this);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    m_cancel = new QPushButton(tr("取消"), this);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        m_cancel->setEnabled(false);
        m_stage = tr("正在取消…");
        refreshStatus();
        emit cancelRequested();
    });
    auto* settings = new QPushButton(tr("设置"), this);
    settings->setFlat(true);
    connect(settings, &QPushButton::clicked, this, &WorkspacePage::settingsRequested);
    auto* top = new QHBoxLayout;
    top->addWidget(m_title);
    top->addSpacing(12);
    top->addWidget(m_status, 1);
    top->addWidget(m_cancel);
    top->addWidget(settings);

    // 提示横幅：识别失败、翻译缺失都在这里说明，不允许"静默成功"
    m_banner = new QWidget(this);
    m_banner->setObjectName(QStringLiteral("banner"));
    m_banner->setAttribute(Qt::WA_StyledBackground);
    m_banner->setStyleSheet(QStringLiteral("#banner { background: rgba(255, 196, 0, 0.22); border-radius: 8px; }"));
    m_bannerText = new QLabel(m_banner);
    m_bannerText->setWordWrap(true);
    m_bannerText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_retry = new QPushButton(tr("重试翻译"), m_banner);
    connect(m_retry, &QPushButton::clicked, this, &WorkspacePage::retranslateRequested);
    auto* bl = new QHBoxLayout(m_banner);
    bl->setContentsMargins(12, 8, 12, 8);
    bl->addWidget(m_bannerText, 1);
    bl->addWidget(m_retry);
    m_banner->hide();

    // 视频 + 播放控制
    m_view = new VideoView(this);
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_view->videoItem());
    m_play = new QPushButton(this);
    m_play->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_play->setFlat(true);
    m_timeline = new Timeline(this);
    m_time = new QLabel(clock(0) + QStringLiteral(" / ") + clock(0), this);
    m_time->setForegroundRole(QPalette::PlaceholderText);
    auto toggle = [this] {
        if (m_player->playbackState() == QMediaPlayer::PlayingState && !m_previewing) {
            m_player->pause();
        } else { // 包括"暂停时跳转正在静音出画面"的过程中按了播放：直接转为正常播放
            m_previewing = false;
            m_audio->setMuted(false);
            m_player->play();
        }
    };
    connect(m_play, &QPushButton::clicked, this, toggle);
    connect(m_view, &VideoView::clicked, this, toggle);
    connect(m_timeline, &Timeline::seekRequested, this, &WorkspacePage::seekTo);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        const bool playing = s == QMediaPlayer::PlayingState && !m_previewing;
        m_play->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
    // 暂停时跳转的新画面到了：停下来
    connect(m_view->videoItem()->videoSink(), &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
        if (m_previewing && std::llabs(frame.startTime() / 1000 - m_previewTarget) < 2000)
            endPreview();
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) {
        m_timeline->setDuration(ms);
        if (ms > 0)
            m_duration = ms / 1000.0;
        m_time->setText(clock(m_player->position() / 1000.0) + QStringLiteral(" / ") + clock(m_duration));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        m_timeline->setPosition(ms);
        m_time->setText(clock(ms / 1000.0) + QStringLiteral(" / ") + clock(m_duration));
        refreshOverlay();
    });
    // macOS 在开始播放前不出画面：加载完成后先把第一帧显示出来
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
        if (st == QMediaPlayer::LoadedMedia && m_player->playbackState() == QMediaPlayer::StoppedState)
            showFrameAt(0);
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString&) {
        m_view->setSubtitle(QString(), QString(), tr("这个格式无法预览，字幕仍会照常生成"));
    });
    auto* controls = new QHBoxLayout;
    controls->addWidget(m_play);
    controls->addWidget(m_timeline, 1);
    controls->addWidget(m_time);
    auto* player = new QWidget(this);
    auto* pl = new QVBoxLayout(player);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(m_view, 1);
    pl->addLayout(controls);

    // 字幕表：单击跳到该行播放，处理完成后双击修改
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    // 长视频几千行：固定行高，不按内容逐行计算高度（字幕一行最多 18 个字，不需要换行）
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setSectionResizeMode(SubtitleModel::Time, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(SubtitleModel::Japanese, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(SubtitleModel::Chinese, QHeaderView::Stretch);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 10);
    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid())
            seekTo(qint64(m_model->segments().at(index.row()).start * 1000) + 1);
    });
    connect(m_model, &SubtitleModel::dataChanged, this, [this] {
        updateBanner();
        refreshOverlay();
        scheduleSave(); // 包括处理完成后的手工修改
    });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(2000);
    connect(&m_saveTimer, &QTimer::timeout, this, &WorkspacePage::saveProject);

    auto* split = new QSplitter(Qt::Vertical, this);
    split->addWidget(player);
    split->addWidget(m_table);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setChildrenCollapsible(false);

    m_newVideo = new QPushButton(tr("处理新视频"), this);
    connect(m_newVideo, &QPushButton::clicked, this, &WorkspacePage::newVideoRequested);
    m_burn = new QPushButton(tr("烧录到视频…"), this);
    connect(m_burn, &QPushButton::clicked, this, &WorkspacePage::burnIn);
    m_export = new QPushButton(tr("导出字幕…"), this);
    m_export->setDefault(true);
    connect(m_export, &QPushButton::clicked, this, &WorkspacePage::exportSubtitles);
    auto* hint = new QLabel(tr("单击一行跳到那里播放，处理完成后双击修改"), this);
    hint->setForegroundRole(QPalette::PlaceholderText);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_newVideo);
    bottom->addWidget(hint, 1, Qt::AlignCenter);
    bottom->addWidget(m_burn);
    bottom->addWidget(m_export);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(top);
    layout->addWidget(m_banner);
    layout->addWidget(split, 1);
    layout->addLayout(bottom);
}

void WorkspacePage::begin(const QString& videoPath, const QString& model, const Project& resume)
{
    m_video = videoPath;
    m_modelId = model;
    m_complete = false;
    m_duration = resume.duration;
    m_translated = 0;
    m_stage.clear();
    m_summary.clear();
    m_status->setToolTip(QString());
    m_failure = {};
    m_translateError = {};
    m_lastTranslateError.clear();
    m_etaBase = -1;
    m_title->setText(QFileInfo(videoPath).fileName());
    // 接着上次的进度：id 按顺序编号，和 Pipeline 里的编号一致
    QList<Segment> lines = resume.segments;
    for (int i = 0; i < lines.size(); ++i) {
        lines[i].id = i;
        if (!lines[i].zh.isEmpty())
            ++m_translated;
    }
    m_model->setSegments(lines);
    m_coverage = resume.covered;
    m_timeline->setProcessed(m_coverage);
    m_view->setSubtitle(QString(), QString());
    m_player->setSource(QUrl::fromLocalFile(videoPath));
    setProcessing(true);
    updateBanner();
}

void WorkspacePage::openResult(const Project& project)
{
    begin(project.video, project.model, project);
    m_complete = true;
    m_summary = tr("上次处理的结果");
    setProcessing(false);
    updateBanner();
}

bool WorkspacePage::covered(double seconds) const
{
    return std::any_of(m_coverage.cbegin(), m_coverage.cend(),
                       [seconds](const TimeRange& r) { return seconds >= r.first && seconds < r.second; });
}

void WorkspacePage::showFrameAt(qint64 ms)
{
    // macOS（AVFoundation）暂停状态下跳转不会出新画面：静音播放，收到目标位置的画面后立即暂停
    m_previewing = true;
    m_previewTarget = ms;
    m_audio->setMuted(true);
    m_player->play();
    const int serial = ++m_previewSerial;
    QTimer::singleShot(1500, this, [this, serial] { // 一直没有画面（如纯音频文件）也要停下来
        if (m_previewing && serial == m_previewSerial)
            endPreview();
    });
}

void WorkspacePage::endPreview()
{
    m_previewing = false;
    m_player->pause();
    m_audio->setMuted(false);
}

void WorkspacePage::seekTo(qint64 ms)
{
    const bool paused = m_player->playbackState() != QMediaPlayer::PlayingState || m_previewing;
    m_player->setPosition(ms);
    if (paused)
        showFrameAt(ms);
    if (m_processing && !covered(ms / 1000.0))
        emit priorityRequested(ms / 1000.0);
}

void WorkspacePage::scheduleSave()
{
    if (!m_video.isEmpty())
        m_saveTimer.start();
}

void WorkspacePage::saveProject()
{
    if (m_video.isEmpty() || (m_model->rowCount() == 0 && m_coverage.isEmpty()))
        return;
    Project p;
    p.video = m_video;
    p.model = m_modelId;
    p.duration = m_duration;
    p.covered = m_coverage;
    p.segments = m_model->segments();
    p.complete = m_complete;
    p.save();
}

void WorkspacePage::setProcessing(bool on)
{
    m_processing = on;
    m_cancel->setVisible(on);
    m_cancel->setEnabled(on);
    m_newVideo->setEnabled(!on);
    const bool hasLines = m_model->rowCount() > 0;
    m_burn->setEnabled(!on && hasLines);
    m_export->setEnabled(!on && hasLines);
    // 处理中后台还在写入，表格只读
    m_model->setEditable(!on);
    m_table->setEditTriggers(on ? QAbstractItemView::NoEditTriggers
                                : QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    refreshStatus();
}

void WorkspacePage::setStage(const QString& text)
{
    if (!m_cancel->isEnabled() && m_processing)
        return; // 正在取消
    m_stage = text;
    refreshStatus();
}

void WorkspacePage::addLines(const QList<Segment>& lines)
{
    m_model->insertLines(lines);
    refreshStatus();
    refreshOverlay();
    scheduleSave();
}

void WorkspacePage::addTranslations(const QList<int>& ids, const QStringList& zh)
{
    m_translated += int(zh.size());
    m_model->setTranslations(ids, zh);
    refreshStatus();
}

void WorkspacePage::setCoverage(const QList<TimeRange>& ranges)
{
    m_coverage = ranges;
    m_timeline->setProcessed(ranges);
    double sum = 0;
    for (const TimeRange& r : ranges)
        sum += r.second - r.first;
    if (m_etaBase < 0) { // 从第一次报告开始计时（不算加载模型、也不算上次已经做完的部分）
        m_etaBase = sum;
        m_etaClock.start();
    }
    refreshStatus();
    refreshOverlay();
    scheduleSave();
}

void WorkspacePage::updateSegments(const QList<Segment>& segments)
{
    m_model->replaceSegments(segments);
    refreshOverlay();
}

void WorkspacePage::finish(const PipelineResult& r)
{
    if (r.mediaDuration > 0)
        m_duration = qMax(m_duration, r.mediaDuration);
    m_translateError = r.translateError;
    m_lastTranslateError = r.lastTranslateError;
    // 行数不变时原地刷新，不重置表格（保留滚动位置和当前行）
    m_model->replaceSegments(r.segments);

    QStringList parts;
    if (!r.review.terms.isEmpty())
        parts << tr("统一了 %1 个专有名词").arg(r.review.terms.size());
    if (r.review.jaFixed > 0)
        parts << tr("纠正 %1 处识别错误").arg(r.review.jaFixed);
    const qint64 tokens = r.usage.promptTokens + r.usage.completionTokens;
    if (tokens > 0)
        parts << tr("用量 %1k token").arg(tokens / 1000.0, 0, 'f', 1);
    if (r.usage.cacheHits > 0 && r.usage.requests == 0)
        parts << tr("全部来自缓存");
    m_summary = parts.join(QStringLiteral(" · "));
    QString tip = r.review.summary;
    for (const auto& [ja, zh] : r.review.terms)
        tip += QStringLiteral("\n%1 → %2").arg(ja, zh);
    if (!r.review.error.isEmpty())
        tip += tr("\n校对没有完成：%1").arg(r.review.error);
    m_status->setToolTip(tip.trimmed());
    m_coverage = r.covered.isEmpty() ? QList<TimeRange>{{0, m_duration}} : r.covered;
    m_timeline->setProcessed(m_coverage);
    m_complete = r.untranslated == 0; // 有没翻出来的行：下次打开同一个视频会接着补译
    setProcessing(false);
    saveProject();
    updateBanner();
}

void WorkspacePage::fail(const TaskError& error)
{
    m_failure = error;
    setProcessing(false);
    updateBanner();
}

void WorkspacePage::setRetranslated(const PipelineResult& r)
{
    m_translateError = r.translateError;
    m_lastTranslateError = r.lastTranslateError;
    m_model->setSegments(r.segments);
    setRetranslating(false);
}

void WorkspacePage::setRetranslating(bool on)
{
    m_retry->setEnabled(!on);
    m_retry->setText(on ? tr("翻译中…") : tr("重试翻译"));
    // 补译结果会整体替换表格内容，期间不允许编辑，免得改动被覆盖
    m_model->setEditable(!on);
    m_table->setEditTriggers(on ? QAbstractItemView::NoEditTriggers
                                : QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    updateBanner();
}

void WorkspacePage::stopPlayback()
{
    m_player->stop();
}

QList<Segment> WorkspacePage::segments() const
{
    return m_model->segments();
}

void WorkspacePage::refreshStatus()
{
    const int lines = m_model->rowCount();
    if (!m_processing) {
        QString text = lines > 0 ? tr("共 %1 行字幕").arg(lines) : QString();
        if (!m_summary.isEmpty())
            text += QStringLiteral(" · ") + m_summary;
        m_status->setText(text);
        return;
    }
    if (lines == 0 || m_stage == tr("正在取消…")) {
        m_status->setText(m_stage);
        return;
    }
    double done = 0;
    for (const TimeRange& r : m_coverage)
        done += r.second - r.first;
    QString text = tr("已识别 %1 / %2").arg(clock(done), clock(m_duration));
    // 剩余时间：按本次的识别速度估算（识别完以后只剩翻译收尾，不再显示）
    const double gained = done - m_etaBase;
    if (m_etaBase >= 0 && gained > 30 && done < m_duration - 1 && m_etaClock.elapsed() > 5000) {
        const double rate = gained / (m_etaClock.elapsed() / 1000.0);
        const int minutes = int(std::ceil((m_duration - done) / rate / 60.0));
        text += minutes <= 1 ? tr(" · 预计不到 1 分钟") : tr(" · 预计还要 %1 分钟").arg(minutes);
    }
    text += tr(" · 已翻译 %1 / %2 行").arg(qMin(m_translated, lines)).arg(lines);
    m_status->setText(text);
}

void WorkspacePage::refreshOverlay()
{
    const double t = m_player->position() / 1000.0;
    const int row = m_model->rowAt(t);
    if (row != m_model->currentRow()) {
        m_model->setCurrentRow(row);
        if (row >= 0 && m_player->playbackState() == QMediaPlayer::PlayingState)
            m_table->scrollTo(m_model->index(row, 0), QAbstractItemView::EnsureVisible);
    }
    if (row >= 0) {
        const Segment& s = m_model->segments().at(row);
        if (!s.zh.trimmed().isEmpty())
            m_view->setSubtitle(s.zh, s.ja);
        else
            m_view->setSubtitle(s.ja, QString(), m_processing ? tr("翻译中…") : QString());
    } else if (m_processing && !covered(t)) {
        m_view->setSubtitle(QString(), QString(), tr("识别中…"));
    } else {
        m_view->setSubtitle(QString(), QString());
    }
}

void WorkspacePage::updateBanner()
{
    QString text;
    if (m_failure.isError()) {
        text = tr("<b>处理失败</b>：%1<br>%2").arg(m_failure.message.toHtmlEscaped(), m_failure.hint.toHtmlEscaped());
    } else if (!m_processing && m_model->untranslatedCount() > 0) {
        if (m_translateError.isError())
            text = tr("<b>翻译失败</b>：%1<br>%2").arg(m_translateError.message.toHtmlEscaped(), m_translateError.hint.toHtmlEscaped());
        else
            text = tr("<b>有 %1 行没有翻译成功</b>（已标黄，可以手动填写或重试）").arg(m_model->untranslatedCount())
                   + (m_lastTranslateError.isEmpty() ? QString() : QStringLiteral("<br>") + m_lastTranslateError.toHtmlEscaped());
    }
    m_bannerText->setText(text);
    m_retry->setVisible(!m_failure.isError());
    m_banner->setVisible(!text.isEmpty());
}

void WorkspacePage::exportSubtitles()
{
    const auto format = Settings::outputFormat();
    const QFileInfo fi(m_video);
    const QString suffix = format == SubtitleWriter::Format::Ass ? QStringLiteral("ass") : QStringLiteral("srt");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出字幕"), fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".zh.") + suffix,
        format == SubtitleWriter::Format::Ass ? tr("ASS 字幕 (*.ass)") : tr("SRT 字幕 (*.srt)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!SubtitleWriter::write(m_model->segments(), path, format, Settings::layout(), &error))
        QMessageBox::warning(this, tr("导出失败"), error);
}

void WorkspacePage::burnIn()
{
    // Homebrew 默认的 ffmpeg 没有 libass，不能烧进画面：先说明，再提供软字幕
    bool soft = false;
    if (!Burner::supportsHardSubtitles()) {
        QMessageBox box(QMessageBox::Information, tr("无法烧录进画面"),
                        tr("当前的 ffmpeg 不支持把字幕烧录进画面（编译时没有带 libass）。"), QMessageBox::Cancel, this);
        box.setInformativeText(tr("可以改为把字幕作为字幕轨嵌入视频：不重新编码，几秒完成，在播放器里可以开关。\n\n"
                                  "如果一定要烧录进画面，请安装带 libass 的 ffmpeg，例如：\n"
                                  "brew tap homebrew-ffmpeg/ffmpeg\nbrew install homebrew-ffmpeg/ffmpeg/ffmpeg"));
        QPushButton* embed = box.addButton(tr("嵌入字幕轨"), QMessageBox::AcceptRole);
        box.setDefaultButton(embed);
        box.exec();
        if (box.clickedButton() != embed)
            return;
        soft = true;
    }

    const QFileInfo fi(m_video);
    const QString out = QFileDialog::getSaveFileName(this, soft ? tr("嵌入字幕轨") : tr("烧录字幕到视频"),
                                                     fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".字幕.mp4"),
                                                     tr("MP4 视频 (*.mp4)"));
    if (out.isEmpty())
        return;

    // 硬字幕用 ASS（样式可控），软字幕用 SRT（mov_text 不支持样式），都写到临时目录
    auto* tmp = new QTemporaryDir;
    const QString subs = tmp->filePath(soft ? QStringLiteral("subs.srt") : QStringLiteral("subs.ass"));
    QString error;
    if (!SubtitleWriter::write(m_model->segments(), subs, soft ? SubtitleWriter::Format::Srt : SubtitleWriter::Format::Ass,
                               Settings::layout(), &error)) {
        delete tmp;
        QMessageBox::warning(this, tr("导出失败"), error);
        return;
    }

    auto* dialog = new QProgressDialog(soft ? tr("正在把字幕嵌入视频…") : tr("正在把字幕烧录进视频…"), tr("取消"), 0, 1000, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    auto* burner = new Burner(dialog);
    connect(burner, &Burner::progress, dialog, [dialog](double f) { dialog->setValue(int(f * 1000)); });
    connect(dialog, &QProgressDialog::canceled, burner, &Burner::cancel);
    connect(burner, &Burner::finished, this, [this, dialog, tmp, out](bool ok, const QString& err) {
        dialog->hide(); // 先关掉进度框，再弹结果
        dialog->deleteLater();
        delete tmp;
        if (ok)
            QMessageBox::information(this, tr("完成"), tr("已保存到：\n%1").arg(out));
        else if (!err.isEmpty())
            QMessageBox::warning(this, tr("失败"), err);
    });
    dialog->show();
    if (soft)
        burner->startSoft(m_video, subs, out, m_duration);
    else
        burner->start(m_video, subs, out, m_duration);
}
