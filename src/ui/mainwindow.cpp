#include "mainwindow.h"

#include "core/furigana.h"
#include "core/modelmanager.h"
#include "core/settings.h"
#include "core/subtitlereader.h"
#include "droppage.h"
#include "modeldownloaddialog.h"
#include "settingsdialog.h"
#include "updatechecker.h"
#include "workspacepage.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QUndoStack>

namespace {

QStringList localFiles(const QMimeData* mime)
{
    QStringList files;
    if (!mime || !mime->hasUrls())
        return files;
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile())
            files << url.toLocalFile();
    }
    return files;
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
    file->addAction(tr("导入字幕…"), this, &MainWindow::importSubtitles);
    QAction* prefs = file->addAction(tr("设置…"), this, &MainWindow::openSettings);
    prefs->setShortcut(QKeySequence::Preferences);
    prefs->setMenuRole(QAction::PreferencesRole); // macOS 放到应用菜单里

    auto* editMenu = menuBar()->addMenu(tr("编辑"));
    QAction* undo = m_workspace->undoStack()->createUndoAction(this, tr("撤销"));
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = m_workspace->undoStack()->createRedoAction(this, tr("重做"));
    redo->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undo);
    editMenu->addAction(redo);

    // 检查更新：启动时自动（一天一次，静默），帮助菜单里手动
    auto* updates = new UpdateChecker(this);
    connect(updates, &UpdateChecker::updateAvailable, m_drop, &DropPage::showUpdate);
    auto* help = menuBar()->addMenu(tr("帮助"));
    help->addAction(tr("检查更新…"), this, [this, updates] {
        auto* box = new QObject(this); // 手动检查的结果弹窗，收到一次后断开
        connect(updates, &UpdateChecker::updateAvailable, box, [this, box](const QString& v, const QString& url) {
            box->deleteLater();
            if (QMessageBox::question(this, tr("检查更新"), tr("发现新版本 %1，要打开下载页吗？").arg(v)) == QMessageBox::Yes)
                QDesktopServices::openUrl(QUrl(url));
        });
        connect(updates, &UpdateChecker::upToDate, box, [this, box] {
            box->deleteLater();
            QMessageBox::information(this, tr("检查更新"), tr("已经是最新版本（%1）").arg(QStringLiteral(APP_VERSION)));
        });
        connect(updates, &UpdateChecker::failed, box, [this, box](const QString& e) {
            box->deleteLater();
            QMessageBox::warning(this, tr("检查更新"), tr("检查失败：%1").arg(e));
        });
        updates->check(false);
    });
    QTimer::singleShot(3000, updates, [updates] { updates->check(true); });
    QTimer::singleShot(0, this, &MainWindow::prepareFurigana);

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
    connect(m_pipeline, &Pipeline::coverageChanged, m_workspace, &WorkspacePage::setCoverage);
    connect(m_pipeline, &Pipeline::durationKnown, m_workspace, &WorkspacePage::setDuration);
    connect(m_workspace, &WorkspacePage::priorityRequested, m_pipeline, &Pipeline::prioritize);
    connect(m_pipeline, &Pipeline::segmentsUpdated, m_workspace, &WorkspacePage::updateSegments);
    connect(m_pipeline, &Pipeline::finished, this, [this](const PipelineResult& r) {
        if (m_retranslating) {
            m_retranslating = false;
            m_workspace->setRetranslated(r);
            return;
        }
        m_workspace->finish(r);
        if (!m_queue.isEmpty())
            QTimer::singleShot(1500, this, &MainWindow::startNextInQueue);
        else
            m_batch = false;
    });
    connect(m_pipeline, &Pipeline::failed, this, [this](const TaskError& e) {
        m_retranslating = false;
        m_workspace->fail(e);
        if (!m_queue.isEmpty()) // 一个失败不影响后面的
            QTimer::singleShot(1500, this, &MainWindow::startNextInQueue);
        else
            m_batch = false;
    });
    connect(m_pipeline, &Pipeline::canceled, this, [this] {
        if (m_retranslating) {
            m_retranslating = false;
            m_workspace->setRetranslating(false);
            return;
        }
        m_queue.clear(); // 取消就整个队列都停下
        m_batch = false;
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
    if (!m_pipeline->isRunning() && !localFiles(event->mimeData()).isEmpty()) {
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
    const QStringList files = localFiles(event->mimeData());
    if (!files.isEmpty()) {
        event->acceptProposedAction();
        startFiles(files);
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
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("选择日语视频（可以多选，依次处理）"), QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        tr("视频或音频 (*.mp4 *.mkv *.mov *.avi *.webm *.flv *.ts *.m4v *.mp3 *.m4a *.wav *.flac *.aac *.ogg);;所有文件 (*)"));
    if (!paths.isEmpty())
        startFiles(paths);
}

void MainWindow::startFiles(const QStringList& paths)
{
    if (m_pipeline->isRunning() || paths.isEmpty())
        return;
    m_batch = paths.size() > 1;
    m_queue = paths.mid(1);
    m_workspace->setQueueCount(int(m_queue.size()));
    startFile(paths.first());
}

void MainWindow::startNextInQueue()
{
    if (m_pipeline->isRunning() || m_queue.isEmpty())
        return;
    const QString next = m_queue.takeFirst();
    m_workspace->setQueueCount(int(m_queue.size()));
    startFile(next);
}

void MainWindow::importSubtitles()
{
    if (m_pipeline->isRunning())
        return;
    const QString srt = QFileDialog::getOpenFileName(this, tr("导入字幕"), QString(), tr("SRT 字幕 (*.srt)"));
    if (srt.isEmpty())
        return;
    QList<Segment> segments;
    QString error;
    if (!SubtitleReader::readFile(srt, &segments, &error)) {
        QMessageBox::warning(this, tr("导入失败"), error);
        return;
    }
    QString video = SubtitleReader::findVideoFor(srt);
    if (video.isEmpty()) {
        video = QFileDialog::getOpenFileName(this, tr("选择这份字幕对应的视频"), QFileInfo(srt).absolutePath(),
                                             tr("视频或音频 (*.mp4 *.mkv *.mov *.avi *.webm *.flv *.ts *.m4v *.mp3 *.m4a *.wav);;所有文件 (*)"));
        if (video.isEmpty())
            return;
    }
    m_currentFile = video;
    m_workspace->openImported(video, Settings::asrModel(), segments);
    m_pages->setCurrentWidget(m_workspace);
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
    if (m_pipeline->isRunning())
        return;
    const QString model = Settings::asrModel();
    Project previous = Project::load(path, model);
    if (previous.complete && m_batch) { // 批量时处理过的直接跳过，不逐个询问
        if (!m_queue.isEmpty())
            QTimer::singleShot(0, this, &MainWindow::startNextInQueue);
        else
            m_batch = false;
        return;
    }
    if (previous.complete) {
        QMessageBox box(QMessageBox::Question, tr("已经处理过"), tr("这个视频之前已经生成过字幕。"), QMessageBox::NoButton, this);
        QPushButton* open = box.addButton(tr("打开上次的结果"), QMessageBox::AcceptRole);
        QPushButton* redo = box.addButton(tr("重新处理"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(open);
        box.exec();
        if (box.clickedButton() == open) {
            m_currentFile = path;
            m_workspace->openResult(previous);
            m_pages->setCurrentWidget(m_workspace);
            return;
        }
        if (box.clickedButton() != redo)
            return;
        Project::remove(path);
        previous = {};
    }
    if (!ensureModel() || !ensureApiKey())
        return;
    m_currentFile = path;
    PipelineOptions o;
    o.input = path;
    o.modelPath = ModelManager::modelPath(model);
    o.translator = Settings::translatorConfig();
    o.review = Settings::reviewEnabled();
    // 上次中途退出：接着做（已识别的区间不再识别，已翻译的行不再翻译）
    o.resumeCovered = previous.covered;
    o.resumeSegments = previous.segments;
    m_workspace->begin(path, model, previous);
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
    if (dialog.exec() == QDialog::Accepted) {
        m_drop->refreshSummary();
        prepareFurigana();
        m_workspace->applySettings();
    }
}

void MainWindow::prepareFurigana()
{
    if (!Settings::furigana() || FuriganaTagger::instance().isLoaded() || m_dictDownload)
        return;
    if (Furigana::dictionaryReady()) {
        QString error;
        if (FuriganaTagger::instance().load(Furigana::dictionaryDir(), &error))
            m_workspace->applySettings();
        else
            qWarning("furigana: %s", qPrintable(error));
        return;
    }
    // 字典只有 13 MB：后台静默下载，好了就自动显示振假名；失败下次启动再试
    m_dictDownload = new DictionaryDownloader(this);
    connect(m_dictDownload, &DictionaryDownloader::finished, this, [this](bool ok, const QString& error) {
        m_dictDownload->deleteLater();
        m_dictDownload = nullptr;
        if (ok)
            prepareFurigana();
        else if (!error.isEmpty())
            qWarning("furigana dictionary: %s", qPrintable(error));
    });
    m_dictDownload->start();
}
