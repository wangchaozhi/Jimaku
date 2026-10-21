#pragma once

#include "core/segment.h"

#include <QAbstractTableModel>

// 字幕编辑表：时间（只读）、日文、中文（可编辑）。没有译文的行标出底色
class SubtitleModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Time, Japanese, Chinese, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    void setSegments(const QList<Segment>& segments);
    const QList<Segment>& segments() const { return m_segments; }
    int untranslatedCount() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    QList<Segment> m_segments;
};
