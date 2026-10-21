#pragma once

#include "core/segment.h"

#include <QAbstractTableModel>
#include <QHash>

// 字幕编辑表：时间（只读）、日文、中文（可编辑）。没有译文的行标出底色
class SubtitleModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Time, Japanese, Chinese, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    void setSegments(const QList<Segment>& segments);
    // 边处理边更新：分段识别时行不一定按时间顺序到达，按开始时间插入；译文按 id 对应
    void insertLines(const QList<Segment>& lines);
    void setTranslations(const QList<int>& ids, const QStringList& zh); // 空字符串不覆盖
    void replaceSegments(const QList<Segment>& segments);       // 行数相同时原地刷新（保留滚动和选中）
    void setEditable(bool editable);
    void setCurrentRow(int row); // 正在播放的行加粗
    int currentRow() const { return m_current; }
    // 覆盖 seconds 的行；没有返回 -1
    int rowAt(double seconds) const;
    const QList<Segment>& segments() const { return m_segments; }
    int untranslatedCount() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    void rebuildIndex();

    QList<Segment> m_segments;
    QHash<int, int> m_rowOfId;
    bool m_editable = true;
    int m_current = -1;
};
