#include "mainwindow.h"

#include "core/modelmanager.h"
#include "core/settings.h"
#include "droppage.h"
#include "modeldownloaddialog.h"
#include "settingsdialog.h"
#include "workspacepage.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStackedWidget>
#include <QStandardPaths>

namespace {

QString firstLocalFile(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls())
        return {};
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile())
            return url.toLocalFile();
    }
    return {};
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_pipeline(new Pipeline(this))
{
    setWindowTitle(QStringLiteral("Jimaku"));
    setAcceptDrops(true);
    resize(960, 640);

    m_drop = new DropPage(this);
    m_workspace = new WorkspacePage(this);
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(m_drop);
    m_pages->addWidget(m_workspace);
    setCentralWidget(m_pages);

    auto* file = menuBar()->addMenu(tr("文件"));
    QAction* open = file->addAction(tr("打开视频…"), this, &MainWindow::chooseFile);
    open->setShortcut(QKeySequence::Open);
    QAction* prefs = file->addAction(tr("设置…"), this, &MainWindow::openSettings);
    prefs->setShortcut(QKeySequence::Preferences);
    prefs->setMenuRole(QAction::PreferencesRole); // macOS 放到应用菜单里

    connect(m_drop, &DropPage::chooseFileRequested, this, &MainWindow::chooseFile);
    connect(m_drop, &DropPage::settingsRequested, this, &MainWindow::openSettings);
    connect(m_workspace, &WorkspacePage::settingsRequested, this, &MainWindow::openSettings);
    connect(m_workspace, &WorkspacePage::newVideoRequested, this, &MainWindow::showDropPage);
    connect(m_workspace, &WorkspacePage::retranslateRequested, this, &MainWindow::retranslate);
    connect(m_workspace, &WorkspacePage::cancelRequested, m_pipeline, &Pipeline::cancel);

    // 边处理边推送到工作台
    connect(m_pipeline, &Pipeline::stageChanged, this, [this](Pipeline::Stage s) {
        if (!m_retranslating)
            m_workspace->setStage(Pipeline::stageName(s));
    });
    connect(m_pipeline, &Pipeline::linesAdded, m_workspace, &WorkspacePage::addLines);
    connect(m_pipeline, &Pipeline::translated, m_workspace, &WorkspacePage::addTranslations);
    connect(m_pipeline, &Pipeline::transcribedUntil, m_workspace, &WorkspacePage::setTranscribedUntil);
    connect(m_pipeline, &Pipeline::segmentsUpdated, m_workspace, &WorkspacePage::updateSegments);
    connect(m_pipeline, &Pipeline::finished, this, [this](const PipelineResult& r) {
        if (m_retranslating) {
            m_retranslating = false;
            m_workspace->setRetranslated(r);
            return;
        }
        m_workspace->finish(r);
    });
    connect(m_pipeline, &Pipeline::failed, this, [this](const TaskError& e) {
        m_retranslating = false;
        m_workspace->fail(e);
    });
    connect(m_pipeline, &Pipeline::canceled, this, [this] {
        if (m_retranslating) {
            m_retranslating = false;
            m_workspace->setRetranslating(false);
            return;
        }
        showDropPage();
    });
}

void MainWindow::showDropPage()
{
    m_workspace->stopPlayback();
    m_pages->setCurrentWidget(m_drop);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!m_pipeline->isRunning() && !firstLocalFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        showDropPage();
        m_drop->setHighlighted(true);
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent*)
{
    m_drop->setHighlighted(false);
}

void MainWindow::dropEvent(QDropEvent* event)
{
    m_drop->setHighlighted(false);
    const QString path = firstLocalFile(event->mimeData());
    if (!path.isEmpty()) {
        event->acceptProposedAction();
        startFile(path);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_pipeline->isRunning()
        && QMessageBox::question(this, tr("正在处理"), tr("字幕还没生成完，确定要退出吗？")) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    m_pipeline->cancel();
    event->accept();
}

void MainWindow::chooseFile()
{
    if (m_pipeline->isRunning())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择日语视频"), QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        tr("视频或音频 (*.mp4 *.mkv *.mov *.avi *.webm *.flv *.ts *.m4v *.mp3 *.m4a *.wav *.flac *.aac *.ogg);;所有文件 (*)"));
    if (!path.isEmpty())
        startFile(path);
}

bool MainWindow::ensureModel()
{
    const QString id = Settings::asrModel();
    if (ModelManager::isDownloaded(id))
        return true;
    return ModelDownloadDialog(id, this).exec() == QDialog::Accepted;
}

bool MainWindow::ensureApiKey()
{
    const TranslateProvider p = Settings::provider(Settings::providerId());
    if (!p.needsKey || Settings::hasApiKey(p.id))
        return true;
    bool ok = false;
    const QString key = QInputDialog::getText(this, tr("需要 API key"),
                                              tr("翻译使用 %1，请粘贴 API key（保存在系统钥匙串，可在设置里更换服务）：").arg(p.label),
                                              QLineEdit::Password, QString(), &ok)
                            .trimmed();
    if (!ok || key.isEmpty())
        return false;
    QString error;
    if (!Settings::setApiKey(p.id, key, &error)) {
        QMessageBox::warning(this, tr("保存 key 失败"), error);
        return false;
    }
    m_drop->refreshSummary();
    return true;
}

void MainWindow::startFile(const QString& path)
{
    if (m_pipeline->isRunning() || !ensureModel() || !ensureApiKey())
        return;
    m_currentFile = path;
    PipelineOptions o;
    o.input = path;
    o.modelPath = ModelManager::modelPath(Settings::asrModel());
    o.translator = Settings::translatorConfig();
    o.review = Settings::reviewEnabled();
    m_workspace->begin(path);
    m_pages->setCurrentWidget(m_workspace);
    m_pipeline->start(o);
}

void MainWindow::retranslate()
{
    if (m_pipeline->isRunning() || !ensureApiKey())
        return;
    m_retranslating = true;
    m_workspace->setRetranslating(true);
    m_pipeline->retranslate(m_workspace->segments(), Settings::translatorConfig());
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
        m_drop->refreshSummary();
}
