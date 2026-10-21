#include "editorpage.h"

#include "core/burner.h"
#include "core/settings.h"
#include "core/subtitlewriter.h"
#include "subtitlemodel.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QVBoxLayout>

EditorPage::EditorPage(QWidget* parent)
    : QWidget(parent)
    , m_model(new SubtitleModel(this))
{
    m_title = new QLabel(this);
    QFont f = m_title->font();
    f.setBold(true);
    m_title->setFont(f);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* settings = new QPushButton(tr("设置"), this);
    settings->setFlat(true);
    connect(settings, &QPushButton::clicked, this, &EditorPage::settingsRequested);
    auto* top = new QHBoxLayout;
    top->addWidget(m_title, 1);
    top->addWidget(settings);

    // 翻译缺失提示：黄色横幅，不允许"静默成功"
    m_banner = new QWidget(this);
    m_banner->setObjectName(QStringLiteral("banner"));
    m_banner->setAttribute(Qt::WA_StyledBackground);
    m_banner->setStyleSheet(QStringLiteral("#banner { background: rgba(255, 196, 0, 0.22); border-radius: 8px; }"));
    m_bannerText = new QLabel(m_banner);
    m_bannerText->setWordWrap(true);
    m_bannerText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_retry = new QPushButton(tr("重试翻译"), m_banner);
    connect(m_retry, &QPushButton::clicked, this, &EditorPage::retranslateRequested);
    auto* bl = new QHBoxLayout(m_banner);
    bl->setContentsMargins(12, 8, 12, 8);
    bl->addWidget(m_bannerText, 1);
    bl->addWidget(m_retry);

    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setWordWrap(true);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->horizontalHeader()->setSectionResizeMode(SubtitleModel::Time, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(SubtitleModel::Japanese, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(SubtitleModel::Chinese, QHeaderView::Stretch);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(m_model, &SubtitleModel::dataChanged, this, &EditorPage::updateBanner);

    auto* newVideo = new QPushButton(tr("处理新视频"), this);
    connect(newVideo, &QPushButton::clicked, this, &EditorPage::newVideoRequested);
    auto* burn = new QPushButton(tr("烧录到视频…"), this);
    connect(burn, &QPushButton::clicked, this, &EditorPage::burnIn);
    auto* exportBtn = new QPushButton(tr("导出字幕…"), this);
    exportBtn->setDefault(true);
    connect(exportBtn, &QPushButton::clicked, this, &EditorPage::exportSubtitles);
    auto* hint = new QLabel(tr("双击单元格修改"), this);
    hint->setForegroundRole(QPalette::PlaceholderText);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(newVideo);
    bottom->addWidget(hint, 1, Qt::AlignCenter);
    bottom->addWidget(burn);
    bottom->addWidget(exportBtn);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(top);
    layout->addWidget(m_banner);
    layout->addWidget(m_table, 1);
    layout->addLayout(bottom);
}

void EditorPage::setResult(const QString& videoPath, const PipelineResult& result)
{
    m_video = videoPath;
    if (result.mediaDuration > 0)
        m_duration = result.mediaDuration;
    m_translateError = result.translateError;
    m_lastTranslateError = result.lastTranslateError;
    m_title->setText(QFileInfo(videoPath).fileName());
    m_model->setSegments(result.segments);
    setRetranslating(false);
}

QList<Segment> EditorPage::segments() const
{
    return m_model->segments();
}

void EditorPage::setRetranslating(bool on)
{
    m_retry->setEnabled(!on);
    m_retry->setText(on ? tr("翻译中…") : tr("重试翻译"));
    updateBanner();
}

void EditorPage::updateBanner()
{
    const int missing = m_model->untranslatedCount();
    if (missing == 0) {
        m_banner->hide();
        return;
    }
    QString text;
    if (m_translateError.isError())
        text = tr("<b>翻译失败</b>：%1<br>%2").arg(m_translateError.message.toHtmlEscaped(), m_translateError.hint.toHtmlEscaped());
    else
        text = tr("<b>有 %1 行没有翻译成功</b>（已标黄，可以手动填写或重试）").arg(missing)
               + (m_lastTranslateError.isEmpty() ? QString() : QStringLiteral("<br>") + m_lastTranslateError.toHtmlEscaped());
    m_bannerText->setText(text);
    m_banner->show();
}

void EditorPage::exportSubtitles()
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

void EditorPage::burnIn()
{
    const QFileInfo fi(m_video);
    const QString out = QFileDialog::getSaveFileName(this, tr("烧录字幕到视频"),
                                                     fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".字幕.mp4"),
                                                     tr("MP4 视频 (*.mp4)"));
    if (out.isEmpty())
        return;

    // 烧录总是用 ASS（样式可控），写到临时目录
    auto* tmp = new QTemporaryDir;
    const QString ass = tmp->filePath(QStringLiteral("subs.ass"));
    QString error;
    if (!SubtitleWriter::write(m_model->segments(), ass, SubtitleWriter::Format::Ass, Settings::layout(), &error)) {
        delete tmp;
        QMessageBox::warning(this, tr("烧录失败"), error);
        return;
    }

    auto* dialog = new QProgressDialog(tr("正在把字幕烧录进视频…"), tr("取消"), 0, 1000, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    auto* burner = new Burner(dialog);
    connect(burner, &Burner::progress, dialog, [dialog](double f) { dialog->setValue(int(f * 1000)); });
    connect(dialog, &QProgressDialog::canceled, burner, &Burner::cancel);
    connect(burner, &Burner::finished, this, [this, dialog, tmp, out](bool ok, const QString& err) {
        dialog->deleteLater();
        delete tmp;
        if (ok)
            QMessageBox::information(this, tr("烧录完成"), tr("已保存到：\n%1").arg(out));
        else if (!err.isEmpty())
            QMessageBox::warning(this, tr("烧录失败"), err);
    });
    dialog->show();
    burner->start(m_video, ass, out, m_duration);
}
