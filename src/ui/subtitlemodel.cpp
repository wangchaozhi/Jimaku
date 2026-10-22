#include "subtitlemodel.h"

#include "core/furigana.h"
#include "core/segmentedit.h"

#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QUndoStack>
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
    rebuildIndex();
    m_current = -1;
    endResetModel();
}

namespace {

// 撤销记录：保存编辑前后的整表（几千行也只是几 MB，换来实现简单、不会出错）
class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(SubtitleModel* model, QList<Segment> before, QList<Segment> after, const QString& text)
        : QUndoCommand(text)
        , m_model(model)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }
    void redo() override { m_model->replaceSegments(m_after); }
    void undo() override { m_model->replaceSegments(m_before); }

private:
    SubtitleModel* m_model;
    QList<Segment> m_before, m_after;
};

} // namespace

void SubtitleModel::applyEdit(QList<Segment> after, const QString& description)
{
    SegmentEdit::sortByStart(&after);
    if (m_undo)
        m_undo->push(new SnapshotCommand(this, m_segments, after, description));
    else
        replaceSegments(after);
}

int SubtitleModel::nextId() const
{
    int id = 0;
    for (const Segment& s : m_segments)
        id = qMax(id, s.id + 1);
    return id;
}

void SubtitleModel::rebuildIndex()
{
    m_rowOfId.clear();
    for (int r = 0; r < m_segments.size(); ++r)
        m_rowOfId.insert(m_segments[r].id, r);
}

void SubtitleModel::insertLines(const QList<Segment>& lines)
{
    if (lines.isEmpty())
        return;
    // 一次推送的几行时间上是连续的：整块插到第一个开始时间更晚的行前面
    auto it = std::upper_bound(m_segments.cbegin(), m_segments.cend(), lines.first().start,
                               [](double t, const Segment& s) { return t < s.start; });
    const int row = int(it - m_segments.cbegin());
    beginInsertRows({}, row, row + int(lines.size()) - 1);
    for (int k = 0; k < lines.size(); ++k)
        m_segments.insert(row + k, lines[k]);
    rebuildIndex();
    endInsertRows();
    if (m_current >= row)
        m_current += int(lines.size());
}

void SubtitleModel::setTranslations(const QList<int>& ids, const QStringList& zh)
{
    for (int k = 0; k < ids.size() && k < zh.size(); ++k) {
        const int row = m_rowOfId.value(ids[k], -1);
        if (row < 0 || zh[k].isEmpty())
            continue;
        m_segments[row].zh = zh[k];
        emit dataChanged(index(row, Chinese), index(row, Chinese));
    }
}

void SubtitleModel::replaceSegments(const QList<Segment>& segments)
{
    if (segments.size() != m_segments.size()) {
        setSegments(segments);
        return;
    }
    m_segments = segments;
    rebuildIndex();
    if (!m_segments.isEmpty())
        emit dataChanged(index(0, 0), index(int(m_segments.size()) - 1, ColumnCount - 1));
}

void SubtitleModel::setFurigana(bool on)
{
    if (on == m_furigana)
        return;
    m_furigana = on;
    if (!m_segments.isEmpty())
        emit dataChanged(index(0, Japanese), index(int(m_segments.size()) - 1, Japanese));
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
        n += needsTranslation(s) ? 1 : 0;
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
    if (role == Qt::DisplayRole && index.column() == Japanese && m_furigana)
        return Furigana::toBracketText(FuriganaTagger::instance().annotate(s.ja));
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
    if (role == Qt::BackgroundRole && index.column() == Chinese && needsTranslation(s) && m_editable)
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
    QList<Segment> after = m_segments;
    Segment& s = after[index.row()];
    const QString text = value.toString();
    if (index.column() == Japanese && text != s.ja)
        s.ja = text;
    else if (index.column() == Chinese && text != s.zh)
        s.zh = text;
    else
        return false;
    applyEdit(after, index.column() == Japanese ? tr("修改日文") : tr("修改中文"));
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
