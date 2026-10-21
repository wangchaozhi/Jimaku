#include "subtitlemodel.h"

#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <cmath>

namespace {

QString shortTime(double seconds)
{
    const int tenths = int(std::lround(seconds * 10));
    return QStringLiteral("%1:%2.%3")
        .arg(tenths / 600, 2, 10, QLatin1Char('0'))
        .arg(tenths / 10 % 60, 2, 10, QLatin1Char('0'))
        .arg(tenths % 10);
}

} // namespace

void SubtitleModel::setSegments(const QList<Segment>& segments)
{
    beginResetModel();
    m_segments = segments;
    endResetModel();
}

int SubtitleModel::untranslatedCount() const
{
    int n = 0;
    for (const Segment& s : m_segments)
        n += s.zh.trimmed().isEmpty() ? 1 : 0;
    return n;
}

int SubtitleModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_segments.size());
}

int SubtitleModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant SubtitleModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_segments.size())
        return {};
    const Segment& s = m_segments[index.row()];
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case Time:
            return QStringLiteral("%1 → %2").arg(shortTime(s.start), shortTime(s.end));
        case Japanese:
            return s.ja;
        case Chinese:
            return s.zh;
        }
    }
    if (role == Qt::BackgroundRole && index.column() == Chinese && s.zh.trimmed().isEmpty())
        return QColor(255, 196, 0, 70); // 没翻出来的行，深浅色主题下都看得见
    if (role == Qt::ForegroundRole && index.column() == Time)
        return QGuiApplication::palette().color(QPalette::PlaceholderText);
    if (role == Qt::ToolTipRole && index.column() == Time)
        return tr("%1 秒").arg(s.duration(), 0, 'f', 1);
    return {};
}

bool SubtitleModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || !index.isValid())
        return false;
    Segment& s = m_segments[index.row()];
    if (index.column() == Japanese)
        s.ja = value.toString();
    else if (index.column() == Chinese)
        s.zh = value.toString();
    else
        return false;
    emit dataChanged(index, index);
    return true;
}

QVariant SubtitleModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    switch (section) {
    case Time:
        return tr("时间");
    case Japanese:
        return tr("日文");
    case Chinese:
        return tr("中文");
    }
    return {};
}

Qt::ItemFlags SubtitleModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == Japanese || index.column() == Chinese)
        f |= Qt::ItemIsEditable;
    return f;
}
