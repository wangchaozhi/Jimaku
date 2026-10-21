#include "settingsdialog.h"

#include "core/modelmanager.h"
#include "core/settings.h"
#include "modeldownloaddialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("设置"));

    // 翻译
    m_provider = new QComboBox(this);
    for (const TranslateProvider& p : Settings::providers())
        m_provider->addItem(p.label, p.id);
    m_baseUrl = new QLineEdit(this);
    m_model = new QLineEdit(this);
    m_key = new QLineEdit(this);
    m_key->setEchoMode(QLineEdit::Password);
    m_clearKey = new QPushButton(tr("删除"), this);
    connect(m_clearKey, &QPushButton::clicked, this, [this] {
        QString error;
        if (!Settings::setApiKey(m_loadedProvider, QString(), &error))
            QMessageBox::warning(this, tr("删除失败"), error);
        loadProvider(m_loadedProvider);
    });
    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget(m_key, 1);
    keyRow->addWidget(m_clearKey);
    // 输入框出于安全总是空的，单独一行明确告诉用户 key 是否已经存好
    m_keyStatus = new QLabel(this);
    m_glossary = new QPlainTextEdit(Settings::glossaryText(), this);
    m_glossary->setPlaceholderText(tr("每行一条：日文=中文\n例如：ジュリン=JURIN"));
    m_glossary->setFixedHeight(80);

    auto* translateBox = new QGroupBox(tr("翻译"), this);
    auto* tf = new QFormLayout(translateBox);
    tf->addRow(tr("服务"), m_provider);
    tf->addRow(tr("接口地址"), m_baseUrl);
    tf->addRow(tr("模型"), m_model);
    tf->addRow(tr("API key"), keyRow);
    tf->addRow(QString(), m_keyStatus);
    tf->addRow(tr("术语表"), m_glossary);

    // 识别
    m_asrModel = new QComboBox(this);
    for (const ModelInfo& m : ModelManager::models())
        m_asrModel->addItem(m.label, m.id);
    m_asrModel->setCurrentIndex(m_asrModel->findData(Settings::asrModel()));
    m_asrStatus = new QLabel(this);
    m_download = new QPushButton(tr("下载"), this);
    connect(m_download, &QPushButton::clicked, this, [this] {
        ModelDownloadDialog(m_asrModel->currentData().toString(), this).exec();
        refreshModelStatus();
    });
    connect(m_asrModel, &QComboBox::currentIndexChanged, this, &SettingsDialog::refreshModelStatus);
    auto* statusRow = new QHBoxLayout;
    statusRow->addWidget(m_asrStatus, 1);
    statusRow->addWidget(m_download);
    auto* asrBox = new QGroupBox(tr("识别"), this);
    auto* af = new QFormLayout(asrBox);
    af->addRow(tr("模型"), m_asrModel);
    af->addRow(QString(), statusRow);

    // 输出
    m_format = new QComboBox(this);
    m_format->addItem(tr("SRT（通用）"), int(SubtitleWriter::Format::Srt));
    m_format->addItem(tr("ASS（带样式）"), int(SubtitleWriter::Format::Ass));
    m_format->setCurrentIndex(m_format->findData(int(Settings::outputFormat())));
    m_layout = new QComboBox(this);
    m_layout->addItem(tr("中日双语（中文在上）"), int(SubtitleWriter::Layout::Bilingual));
    m_layout->addItem(tr("仅中文"), int(SubtitleWriter::Layout::ChineseOnly));
    m_layout->addItem(tr("仅日文"), int(SubtitleWriter::Layout::JapaneseOnly));
    m_layout->setCurrentIndex(m_layout->findData(int(Settings::layout())));
    auto* outBox = new QGroupBox(tr("输出"), this);
    auto* of = new QFormLayout(outBox);
    of->addRow(tr("格式"), m_format);
    of->addRow(tr("内容"), m_layout);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(translateBox);
    layout->addWidget(asrBox);
    layout->addWidget(outBox);
    layout->addWidget(buttons);
    setMinimumWidth(520);

    connect(m_provider, &QComboBox::currentIndexChanged, this,
            [this] { loadProvider(m_provider->currentData().toString()); });
    m_provider->setCurrentIndex(m_provider->findData(Settings::providerId()));
    loadProvider(Settings::providerId());
    refreshModelStatus();
}

void SettingsDialog::loadProvider(const QString& id)
{
    // 地址和模型按服务分别保存：先临时切换当前服务再读取
    const QString saved = Settings::providerId();
    Settings::setProviderId(id);
    const TranslateProvider p = Settings::provider(id);
    m_baseUrl->setText(Settings::baseUrl());
    m_baseUrl->setPlaceholderText(p.baseUrl.isEmpty() ? QStringLiteral("https://…/v1") : p.baseUrl);
    m_model->setText(Settings::modelName());
    Settings::setProviderId(saved);

    const QString savedKey = Settings::apiKey(id);
    const bool hasKey = !savedKey.isEmpty();
    m_key->clear();
    m_key->setEnabled(p.needsKey || id == QLatin1String("custom"));
    m_key->setPlaceholderText(!m_key->isEnabled() ? tr("本地服务不需要 key")
                              : hasKey             ? tr("已保存在系统钥匙串（留空则不修改）")
                                                   : tr("粘贴 API key，保存在系统钥匙串"));
    m_clearKey->setEnabled(hasKey);
    if (!m_key->isEnabled())
        m_keyStatus->clear();
    else if (hasKey) // 只露出末尾 4 位，方便核对是哪一个 key
        m_keyStatus->setText(tr("✓ 已保存在系统钥匙串（…%1）").arg(savedKey.right(4)));
    else
        m_keyStatus->setText(tr("还没有保存 key，翻译前需要填写"));
    m_loadedProvider = id;
}

void SettingsDialog::refreshModelStatus()
{
    const QString id = m_asrModel->currentData().toString();
    const bool ok = ModelManager::isDownloaded(id);
    m_asrStatus->setText(ok ? tr("已下载") : tr("未下载（约 %1）").arg(QLocale().formattedDataSize(ModelManager::find(id).approxSize)));
    m_download->setVisible(!ok);
}

void SettingsDialog::accept()
{
    const QString id = m_provider->currentData().toString();
    Settings::setProviderId(id);
    Settings::setBaseUrl(m_baseUrl->text());
    Settings::setModelName(m_model->text());
    if (!m_key->text().trimmed().isEmpty()) {
        QString error;
        if (!Settings::setApiKey(id, m_key->text().trimmed(), &error)) {
            QMessageBox::warning(this, tr("保存 key 失败"), error);
            return;
        }
    }
    Settings::setGlossaryText(m_glossary->toPlainText());
    Settings::setAsrModel(m_asrModel->currentData().toString());
    Settings::setOutputFormat(SubtitleWriter::Format(m_format->currentData().toInt()));
    Settings::setLayout(SubtitleWriter::Layout(m_layout->currentData().toInt()));
    QDialog::accept();
}
