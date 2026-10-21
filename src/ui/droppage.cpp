#include "droppage.h"

#include "core/modelmanager.h"
#include "core/settings.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

DropPage::DropPage(QWidget* parent)
    : QWidget(parent)
{
    m_zone = new QWidget(this);
    m_zone->setObjectName(QStringLiteral("dropZone"));
    m_zone->setAttribute(Qt::WA_StyledBackground);

    auto* title = new QLabel(tr("把日语视频拖到这里"), m_zone);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.8);
    f.setBold(true);
    title->setFont(f);
    title->setAlignment(Qt::AlignCenter);

    auto* sub = new QLabel(tr("自动识别日语、断句并翻译成中文，生成中日双语字幕"), m_zone);
    sub->setAlignment(Qt::AlignCenter);
    sub->setForegroundRole(QPalette::PlaceholderText);

    auto* choose = new QPushButton(tr("选择视频…"), m_zone);
    choose->setDefault(true);
    choose->setMinimumWidth(160);
    connect(choose, &QPushButton::clicked, this, &DropPage::chooseFileRequested);

    auto* zoneLayout = new QVBoxLayout(m_zone);
    zoneLayout->addStretch();
    zoneLayout->addWidget(title);
    zoneLayout->addSpacing(6);
    zoneLayout->addWidget(sub);
    zoneLayout->addSpacing(18);
    zoneLayout->addWidget(choose, 0, Qt::AlignCenter);
    zoneLayout->addStretch();

    m_summary = new QLabel(this);
    m_summary->setForegroundRole(QPalette::PlaceholderText);
    auto* settings = new QPushButton(tr("设置"), this);
    settings->setFlat(true);
    connect(settings, &QPushButton::clicked, this, &DropPage::settingsRequested);

    m_update = new QLabel(this);
    m_update->setOpenExternalLinks(true);
    m_update->hide();
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_summary, 1);
    bottom->addWidget(m_update);
    bottom->addWidget(settings);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 12);
    layout->addWidget(m_zone, 1);
    layout->addLayout(bottom);

    setHighlighted(false);
    refreshSummary();
}

void DropPage::setHighlighted(bool on)
{
    // palette(highlight) 跟随系统强调色，深浅色主题都可用
    m_zone->setStyleSheet(on ? QStringLiteral("#dropZone { border: 2px dashed palette(highlight); border-radius: 16px;"
                                              " background: palette(alternate-base); }")
                             : QStringLiteral("#dropZone { border: 2px dashed palette(mid); border-radius: 16px; }"));
}

void DropPage::showUpdate(const QString& version, const QString& url)
{
    m_update->setText(tr("<a href=\"%1\">发现新版本 %2，点此下载</a>").arg(url.toHtmlEscaped(), version.toHtmlEscaped()));
    m_update->show();
}

void DropPage::refreshSummary()
{
    const TranslateProvider p = Settings::provider(Settings::providerId());
    QString translate = p.label;
    if (p.needsKey && !Settings::hasApiKey(p.id))
        translate += tr("（未设置 key）");
    m_summary->setText(tr("识别：%1    翻译：%2").arg(ModelManager::find(Settings::asrModel()).id, translate));
}
