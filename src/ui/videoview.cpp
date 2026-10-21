#include "videoview.h"

#include <QGraphicsItem>
#include <QGraphicsVideoItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

// 在视频区域底部（或顶部）居中画两行带描边的字幕（不依赖播放器自带的字幕渲染）
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
    void setRuby(const QList<RubyPart>& ruby, bool onPrimary)
    {
        m_ruby = ruby;
        m_rubyOnPrimary = onPrimary;
        update();
    }
    void setAppearance(const SubtitleWriter::Appearance& a)
    {
        m_appearance = a;
        update();
    }
    QRectF boundingRect() const override { return m_rect; }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        if (m_rect.isEmpty())
            return;
        p->setRenderHint(QPainter::Antialiasing);
        const qreal h = m_rect.height();
        const bool top = m_appearance.top;
        const qreal k = m_appearance.scale;
        // 底部：自下而上画 提示、日文、中文；顶部：自上而下画 中文、日文、提示
        qreal cursor = top ? m_rect.top() + h * 0.05 : m_rect.bottom() - h * 0.05;
        // ruby 非空时在汉字上方画小号假名（这一行要多留出注音的高度）
        auto line = [&](const QString& text, qreal size, const QColor& fill, const QList<RubyPart>& ruby = {}) {
            if (text.isEmpty())
                return;
            QFont f;
            f.setPixelSize(qMax(10, int(size)));
            f.setBold(true);
            const QFontMetricsF fm(f);
            QFont rf = f;
            rf.setPixelSize(qMax(7, int(size * 0.45)));
            const QFontMetricsF rfm(rf);
            const bool hasRuby = std::any_of(ruby.cbegin(), ruby.cend(), [](const RubyPart& r) { return !r.ruby.isEmpty(); });
            const qreal rubyHeight = hasRuby ? rfm.height() * 0.95 : 0;
            const QString shown = hasRuby ? text : fm.elidedText(text, Qt::ElideRight, m_rect.width() * 0.94);
            QPainterPath path;
            const qreal baseline = top ? cursor + rubyHeight + fm.ascent() : cursor - fm.descent();
            qreal x = m_rect.center().x() - fm.horizontalAdvance(shown) / 2;
            if (hasRuby) {
                for (const RubyPart& part : ruby) {
                    const qreal w = fm.horizontalAdvance(part.text);
                    path.addText(x, baseline, f, part.text);
                    if (!part.ruby.isEmpty())
                        path.addText(x + (w - rfm.horizontalAdvance(part.ruby)) / 2, baseline - fm.ascent() - rfm.descent(), rf, part.ruby);
                    x += w;
                }
            } else {
                path.addText(x, baseline, f, shown);
            }
            p->setPen(QPen(QColor(0, 0, 0, 220), size * 0.16, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawPath(path);
            cursor += (top ? 1 : -1) * (fm.height() * 1.05 + rubyHeight);
        };
        const QList<RubyPart> primaryRuby = m_rubyOnPrimary ? m_ruby : QList<RubyPart>();
        const QList<RubyPart> secondaryRuby = m_rubyOnPrimary ? QList<RubyPart>() : m_ruby;
        if (top) {
            line(m_primary, h * 0.062 * k, Qt::white, primaryRuby);
            line(m_secondary, h * 0.045 * k, QColor(235, 235, 235), secondaryRuby);
            line(m_hint, h * 0.040, QColor(200, 200, 200));
        } else {
            line(m_hint, h * 0.040, QColor(200, 200, 200));
            line(m_secondary, h * 0.045 * k, QColor(235, 235, 235), secondaryRuby);
            line(m_primary, h * 0.062 * k, Qt::white, primaryRuby);
        }
    }

private:
    QRectF m_rect;
    QString m_primary, m_secondary, m_hint;
    SubtitleWriter::Appearance m_appearance;
    QList<RubyPart> m_ruby;
    bool m_rubyOnPrimary = false;
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

void VideoView::setRuby(const QList<RubyPart>& ruby, bool onPrimary)
{
    m_subtitle->setRuby(ruby, onPrimary);
}

void VideoView::setAppearance(const SubtitleWriter::Appearance& appearance)
{
    m_subtitle->setAppearance(appearance);
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
