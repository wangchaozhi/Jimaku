#include "progresspage.h"

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QLabel* heading(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    f.setBold(true);
    l->setFont(f);
    l->setAlignment(Qt::AlignCenter);
    l->setWordWrap(true);
    return l;
}

} // namespace

ProgressPage::ProgressPage(QWidget* parent)
    : QWidget(parent)
{
    // 运行中
    auto* running = new QWidget(this);
    m_file = heading(QString(), running);
    m_stage = new QLabel(running);
    m_stage->setAlignment(Qt::AlignCenter);
    m_bar = new QProgressBar(running);
    m_bar->setRange(0, 1000);
    m_bar->setTextVisible(false);
    m_bar->setMaximumWidth(520);
    m_cancel = new QPushButton(tr("取消"), running);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        m_cancel->setEnabled(false);
        m_stage->setText(tr("正在取消…"));
        emit cancelRequested();
    });
    auto* rl = new QVBoxLayout(running);
    rl->addStretch();
    rl->addWidget(m_file);
    rl->addSpacing(12);
    rl->addWidget(m_stage);
    rl->addWidget(m_bar, 0, Qt::AlignCenter);
    rl->addSpacing(12);
    rl->addWidget(m_cancel, 0, Qt::AlignCenter);
    rl->addStretch();

    // 失败
    auto* failed = new QWidget(this);
    m_errorText = new QLabel(failed);
    m_errorText->setWordWrap(true);
    m_errorText->setAlignment(Qt::AlignCenter);
    m_errorText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_errorText->setMaximumWidth(560);
    m_errorHint = new QLabel(failed);
    m_errorHint->setWordWrap(true);
    m_errorHint->setAlignment(Qt::AlignCenter);
    m_errorHint->setMaximumWidth(560);
    m_errorHint->setForegroundRole(QPalette::PlaceholderText);
    auto* back = new QPushButton(tr("返回"), failed);
    back->setDefault(true);
    connect(back, &QPushButton::clicked, this, &ProgressPage::backRequested);
    auto* fl = new QVBoxLayout(failed);
    fl->addStretch();
    fl->addWidget(heading(tr("处理失败"), failed));
    fl->addSpacing(8);
    fl->addWidget(m_errorText, 0, Qt::AlignCenter);
    fl->addWidget(m_errorHint, 0, Qt::AlignCenter);
    fl->addSpacing(16);
    fl->addWidget(back, 0, Qt::AlignCenter);
    fl->addStretch();

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(running);
    m_stack->addWidget(failed);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_stack);
}

void ProgressPage::showRunning(const QString& fileName)
{
    m_file->setText(fileName);
    m_stage->clear();
    m_bar->setValue(0);
    m_cancel->setEnabled(true);
    m_stack->setCurrentIndex(0);
}

void ProgressPage::setStage(const QString& text)
{
    if (m_cancel->isEnabled())
        m_stage->setText(text);
}

void ProgressPage::setProgress(double fraction)
{
    m_bar->setValue(int(fraction * 1000));
}

void ProgressPage::showError(const TaskError& error)
{
    m_errorText->setText(error.message);
    m_errorHint->setText(error.hint);
    m_errorHint->setVisible(!error.hint.isEmpty());
    m_stack->setCurrentIndex(1);
}
