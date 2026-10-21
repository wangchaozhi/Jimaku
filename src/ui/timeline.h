#pragma once

#include <QWidget>

// 播放进度条：已识别的部分着色，拖动或点击跳转
class Timeline : public QWidget {
    Q_OBJECT
public:
    explicit Timeline(QWidget* parent = nullptr);

    void setDuration(qint64 ms);
    void setPosition(qint64 ms);
    void setProcessed(qint64 ms); // 已识别到的位置

    QSize sizeHint() const override { return {400, 22}; }

signals:
    void seekRequested(qint64 ms);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QRectF track() const;
    qint64 msAt(qreal x) const;

    qint64 m_duration = 0;
    qint64 m_position = 0;
    qint64 m_processed = 0;
};
