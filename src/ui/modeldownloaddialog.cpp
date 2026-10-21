#include "modeldownloaddialog.h"

#include "core/modelmanager.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

ModelDownloadDialog::ModelDownloadDialog(const QString& modelId, QWidget* parent)
    : QDialog(parent)
    , m_modelId(modelId)
    , m_manager(new ModelManager(this))
{
    setWindowTitle(tr("下载识别模型"));
    const ModelInfo info = ModelManager::find(modelId);
    auto* intro = new QLabel(tr("首次使用需要下载识别模型 %1（约 %2），只需下载一次。")
                                 .arg(info.label, QLocale().formattedDataSize(info.approxSize)),
                             this);
    intro->setWordWrap(true);
    m_text = new QLabel(tr("正在连接…"), this);
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 1000);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::rejected, m_manager, &ModelManager::cancel);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_bar);
    layout->addWidget(m_text);
    layout->addWidget(buttons);
    setMinimumWidth(460);

    connect(m_manager, &ModelManager::progress, this, [this](qint64 got, qint64 total, double speed) {
        const QLocale loc;
        if (total > 0)
            m_bar->setValue(int(1000.0 * got / total));
        m_text->setText(tr("%1 / %2    %3/s")
                            .arg(loc.formattedDataSize(got), total > 0 ? loc.formattedDataSize(total) : QStringLiteral("?"),
                                 loc.formattedDataSize(qint64(speed))));
    });
    connect(m_manager, &ModelManager::finished, this, [this](bool ok, const QString& error) {
        if (ok) {
            accept();
            return;
        }
        if (!error.isEmpty())
            QMessageBox::warning(this, tr("下载失败"), error);
        reject();
    });
}

int ModelDownloadDialog::exec()
{
    m_manager->download(m_modelId);
    return QDialog::exec();
}
