#pragma once

#include <QGraphicsView>

class QGraphicsVideoItem;
class SubtitleItem;

// 视频画面 + 叠加在画面底部的字幕。
// 用 QGraphicsVideoItem 而不是 QVideoWidget：后者在 macOS 上是原生窗口，普通控件盖不上去
class VideoView : public QGraphicsView {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);

    QGraphicsVideoItem* videoItem() const { return m_video; }
    // 主字幕（中文，大）、副字幕（日文，小）、提示（灰色，如"翻译中…"）
    void setSubtitle(const QString& primary, const QString& secondary, const QString& hint = QString());

signals:
    void clicked();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void relayout();

    QGraphicsVideoItem* m_video;
    SubtitleItem* m_subtitle;
};
