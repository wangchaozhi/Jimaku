#include "timeline.h"

#include <QMouseEvent>
#include <QPainter>

Timeline::Timeline(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(22);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Timeline::setDuration(qint64 ms)
{
    m_duration = qMax<qint64>(0, ms);
    update();
}

void Timeline::setPosition(qint64 ms)
{
    m_position = ms;
    update();
}

void Timeline::setProcessed(qint64 ms)
{
    m_processed = ms;
    update();
}

QRectF Timeline::track() const
{
    return QRectF(7, height() / 2.0 - 2, width() - 14, 4);
}

qint64 Timeline::msAt(qreal x) const
{
    const QRectF t = track();
    return qint64(qBound(0.0, (x - t.left()) / t.width(), 1.0) * m_duration);
}

void Timeline::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const QRectF t = track();
    auto part = [&](qint64 ms) {
        QRectF r = t;
        r.setWidth(m_duration > 0 ? t.width() * qBound(0.0, double(ms) / m_duration, 1.0) : 0);
        return r;
    };
    p.setBrush(palette().color(QPalette::Mid));
    p.drawRoundedRect(t, 2, 2);
    QColor processed = palette().color(QPalette::Highlight); // 已识别：强调色的浅色
    processed.setAlpha(90);
    p.setBrush(processed);
    p.drawRoundedRect(part(m_processed), 2, 2);
    p.setBrush(palette().color(QPalette::Highlight));
    p.drawRoundedRect(part(m_position), 2, 2);
    const QRectF played = part(m_position);
    p.setBrush(palette().color(QPalette::Light));
    p.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
    p.drawEllipse(QPointF(played.right(), t.center().y()), 6, 6);
}

void Timeline::mousePressEvent(QMouseEvent* event)
{
    if (m_duration > 0)
        emit seekRequested(msAt(event->position().x()));
}

void Timeline::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && m_duration > 0)
        emit seekRequested(msAt(event->position().x()));
}
