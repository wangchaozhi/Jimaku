#include "coverage.h"

#include <algorithm>

namespace {

constexpr double kEpsilon = 0.05; // 50 ms 以内的缝当作连着

} // namespace

Coverage::Coverage(double duration)
    : m_duration(duration)
{
}

void Coverage::add(double from, double to)
{
    from = qMax(0.0, from);
    to = qMin(m_duration, to);
    if (to - from <= 0)
        return;
    m_ranges.append({from, to});
    std::sort(m_ranges.begin(), m_ranges.end());
    QList<TimeRange> merged;
    for (const TimeRange& r : m_ranges) {
        if (!merged.isEmpty() && r.first <= merged.last().second + kEpsilon)
            merged.last().second = qMax(merged.last().second, r.second);
        else
            merged.append(r);
    }
    m_ranges = merged;
}

bool Coverage::covers(double t) const
{
    return std::any_of(m_ranges.cbegin(), m_ranges.cend(), [t](const TimeRange& r) { return t >= r.first && t < r.second; });
}

bool Coverage::complete() const
{
    return m_ranges.size() == 1 && m_ranges.first().first <= kEpsilon && m_ranges.first().second >= m_duration - kEpsilon;
}

double Coverage::coveredSeconds() const
{
    double sum = 0;
    for (const TimeRange& r : m_ranges)
        sum += r.second - r.first;
    return sum;
}

double Coverage::nextStart(double priority) const
{
    if (complete())
        return -1;
    // 所有空隙
    QList<TimeRange> gaps;
    double cursor = 0;
    for (const TimeRange& r : m_ranges) {
        if (r.first > cursor + kEpsilon)
            gaps.append({cursor, r.first});
        cursor = qMax(cursor, r.second);
    }
    if (cursor < m_duration - kEpsilon)
        gaps.append({cursor, m_duration});
    if (gaps.isEmpty())
        return -1;
    if (priority >= 0) {
        for (const TimeRange& g : gaps) {
            if (priority >= g.first && priority < g.second)
                return priority - g.first < 5.0 ? g.first : priority;
        }
    }
    return gaps.first().first;
}

double Coverage::limitAfter(double start) const
{
    for (const TimeRange& r : m_ranges) {
        if (r.first > start + kEpsilon)
            return r.first;
    }
    return m_duration;
}
