#include "subtitlemodel.h"

#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <algorithm>
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

void SubtitleModel::appendLines(int firstIndex, const QList<Segment>& lines)
{
    if (firstIndex != m_segments.size() || lines.isEmpty())
        return;
    beginInsertRows({}, firstIndex, firstIndex + int(lines.size()) - 1);
    m_segments += lines;
    endInsertRows();
}

void SubtitleModel::setTranslations(int firstIndex, const QStringList& zh)
{
    int last = -1;
    for (int k = 0; k < zh.size(); ++k) {
        const int row = firstIndex + k;
        if (row < 0 || row >= m_segments.size() || zh[k].isEmpty())
            continue;
        m_segments[row].zh = zh[k];
        last = row;
    }
    if (last >= 0)
        emit dataChanged(index(firstIndex, Chinese), index(last, Chinese));
}

void SubtitleModel::replaceSegments(const QList<Segment>& segments)
{
    if (segments.size() != m_segments.size()) {
        setSegments(segments);
        return;
    }
    m_segments = segments;
    if (!m_segments.isEmpty())
        emit dataChanged(index(0, 0), index(int(m_segments.size()) - 1, ColumnCount - 1));
}

void SubtitleModel::setEditable(bool editable)
{
    m_editable = editable;
}

void SubtitleModel::setCurrentRow(int row)
{
    if (row == m_current)
        return;
    const int old = m_current;
    m_current = row;
    for (int r : {old, row}) {
        if (r >= 0 && r < m_segments.size())
            emit dataChanged(index(r, 0), index(r, ColumnCount - 1), {Qt::FontRole});
    }
}

int SubtitleModel::rowAt(double seconds) const
{
    // 行按开始时间递增：找最后一个 start <= seconds 的行，再看是否还没结束
    auto it = std::upper_bound(m_segments.cbegin(), m_segments.cend(), seconds,
                               [](double t, const Segment& s) { return t < s.start; });
    if (it == m_segments.cbegin())
        return -1;
    --it;
    return seconds <= it->end ? int(it - m_segments.cbegin()) : -1;
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
    if (role == Qt::FontRole && index.row() == m_current) {
        QFont f;
        f.setBold(true);
        return f;
    }
    if (role == Qt::BackgroundRole && index.column() == Chinese && s.zh.trimmed().isEmpty() && m_editable)
        return QColor(255, 196, 0, 70); // 没翻出来的行，深浅色主题下都看得见
    // 校对时改过的日文：浅蓝底，悬停显示语音识别的原文
    if (role == Qt::BackgroundRole && index.column() == Japanese && !s.jaOriginal.isEmpty())
        return QColor(80, 160, 255, 50);
    if (role == Qt::ToolTipRole && index.column() == Japanese && !s.jaOriginal.isEmpty())
        return tr("已按上下文纠正识别错误\n识别原文：%1").arg(s.jaOriginal);
    if (role == Qt::BackgroundRole && index.column() == Chinese && !s.zhOriginal.isEmpty() && !s.zh.trimmed().isEmpty())
        return QColor(80, 160, 255, 50);
    if (role == Qt::ToolTipRole && index.column() == Chinese && !s.zhOriginal.isEmpty())
        return tr("全片校对改过这一行\n校对前：%1").arg(s.zhOriginal);
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
    if (m_editable && (index.column() == Japanese || index.column() == Chinese))
        f |= Qt::ItemIsEditable;
    return f;
}
