#include "videoview.h"

#include <QGraphicsItem>
#include <QGraphicsVideoItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

// 在视频区域底部居中画两行带描边的字幕（不依赖播放器自带的字幕渲染）
class SubtitleItem : public QGraphicsItem {
public:
    void setVideoRect(const QRectF& r)
    {
        prepareGeometryChange();
        m_rect = r;
    }
    void setText(const QString& primary, const QString& secondary, const QString& hint)
    {
        if (primary == m_primary && secondary == m_secondary && hint == m_hint)
            return;
        m_primary = primary;
        m_secondary = secondary;
        m_hint = hint;
        update();
    }
    QRectF boundingRect() const override { return m_rect; }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        if (m_rect.isEmpty())
            return;
        p->setRenderHint(QPainter::Antialiasing);
        const qreal h = m_rect.height();
        qreal bottom = m_rect.bottom() - h * 0.05;
        // 自下而上：提示、日文、中文
        auto line = [&](const QString& text, qreal size, const QColor& fill) {
            if (text.isEmpty())
                return;
            QFont f;
            f.setPixelSize(qMax(10, int(size)));
            f.setBold(true);
            const QFontMetricsF fm(f);
            const QString shown = fm.elidedText(text, Qt::ElideRight, m_rect.width() * 0.94);
            QPainterPath path;
            path.addText(m_rect.center().x() - fm.horizontalAdvance(shown) / 2, bottom - fm.descent(), f, shown);
            p->setPen(QPen(QColor(0, 0, 0, 220), size * 0.16, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawPath(path);
            bottom -= fm.height() * 1.05;
        };
        line(m_hint, h * 0.040, QColor(200, 200, 200));
        line(m_secondary, h * 0.045, QColor(235, 235, 235));
        line(m_primary, h * 0.062, Qt::white);
    }

private:
    QRectF m_rect;
    QString m_primary, m_secondary, m_hint;
};

VideoView::VideoView(QWidget* parent)
    : QGraphicsView(parent)
    , m_video(new QGraphicsVideoItem)
    , m_subtitle(new SubtitleItem)
{
    auto* scene = new QGraphicsScene(this);
    scene->addItem(m_video);
    scene->addItem(m_subtitle);
    m_subtitle->setZValue(1);
    setScene(scene);
    setBackgroundBrush(Qt::black);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumHeight(200);
    connect(m_video, &QGraphicsVideoItem::nativeSizeChanged, this, &VideoView::relayout);
}

void VideoView::setSubtitle(const QString& primary, const QString& secondary, const QString& hint)
{
    m_subtitle->setText(primary, secondary, hint);
}

void VideoView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    relayout();
}

void VideoView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QGraphicsView::mousePressEvent(event);
}

void VideoView::relayout()
{
    const QRectF area(QPointF(0, 0), QSizeF(viewport()->size()));
    scene()->setSceneRect(area);
    m_video->setPos(0, 0);
    m_video->setSize(area.size());
    // 字幕画在视频实际显示的区域内（保持宽高比后的那块）；还没有画面时用整个区域
    const QRectF shown = m_video->boundingRect();
    m_subtitle->setVideoRect(shown.isEmpty() ? area : shown);
}
