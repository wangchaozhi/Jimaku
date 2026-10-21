#include "project.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {

QJsonObject toJson(const Segment& s)
{
    QJsonObject o{{QStringLiteral("start"), s.start}, {QStringLiteral("end"), s.end}, {QStringLiteral("ja"), s.ja},
                  {QStringLiteral("zh"), s.zh}};
    if (!s.jaOriginal.isEmpty())
        o.insert(QStringLiteral("jaOriginal"), s.jaOriginal);
    if (!s.zhOriginal.isEmpty())
        o.insert(QStringLiteral("zhOriginal"), s.zhOriginal);
    return o;
}

Segment fromJson(const QJsonObject& o)
{
    Segment s;
    s.start = o.value(QStringLiteral("start")).toDouble();
    s.end = o.value(QStringLiteral("end")).toDouble();
    s.ja = o.value(QStringLiteral("ja")).toString();
    s.zh = o.value(QStringLiteral("zh")).toString();
    s.jaOriginal = o.value(QStringLiteral("jaOriginal")).toString();
    s.zhOriginal = o.value(QStringLiteral("zhOriginal")).toString();
    return s;
}

} // namespace

QString Project::fileFor(const QString& video)
{
    const QByteArray key = QCryptographicHash::hash(QFileInfo(video).absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/projects/")
           + QString::fromLatin1(key) + QStringLiteral(".json");
}

Project Project::load(const QString& video, const QString& model)
{
    QFile f(fileFor(video));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QFileInfo fi(video);
    Project p;
    p.video = o.value(QStringLiteral("video")).toString();
    p.size = o.value(QStringLiteral("size")).toInteger();
    p.modified = QDateTime::fromString(o.value(QStringLiteral("modified")).toString(), Qt::ISODateWithMs);
    p.model = o.value(QStringLiteral("model")).toString();
    // 视频文件变了、或者换了识别模型：之前的结果不再适用
    if (p.size != fi.size() || p.modified != fi.lastModified() || p.model != model)
        return {};
    p.duration = o.value(QStringLiteral("duration")).toDouble();
    p.complete = o.value(QStringLiteral("complete")).toBool();
    for (const QJsonValue& v : o.value(QStringLiteral("covered")).toArray())
        p.covered.append({v.toArray().at(0).toDouble(), v.toArray().at(1).toDouble()});
    for (const QJsonValue& v : o.value(QStringLiteral("segments")).toArray())
        p.segments.append(fromJson(v.toObject()));
    p.segments = sanitizeSegments(p.segments, p.covered, p.complete);
    return p;
}

QList<Segment> sanitizeSegments(QList<Segment> segments, const QList<TimeRange>& covered, bool complete)
{
    std::stable_sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b) { return a.start < b.start; });
    QList<Segment> out;
    for (const Segment& s : segments) {
        const bool inside = std::any_of(covered.cbegin(), covered.cend(),
                                        [&](const TimeRange& r) { return s.start >= r.first - 0.05 && s.start < r.second; });
        if (!complete && !inside)
            continue;
        // 和前面某一行时间、文字都相同：重复（只需要和开始时间相同的那几行比）
        bool duplicate = false;
        for (int i = int(out.size()) - 1; i >= 0 && s.start - out[i].start < 0.01; --i) {
            if (std::abs(out[i].end - s.end) < 0.01 && out[i].ja == s.ja) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            out.append(s);
    }
    return out;
}

bool Project::save(QString* error) const
{
    const QString path = fileFor(video);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray covered, segs;
    for (const TimeRange& r : this->covered)
        covered.append(QJsonArray{r.first, r.second});
    for (const Segment& s : segments)
        segs.append(toJson(s));
    const QFileInfo fi(video);
    const QJsonObject o{{QStringLiteral("video"), fi.absoluteFilePath()},
                        {QStringLiteral("size"), fi.size()},
                        {QStringLiteral("modified"), fi.lastModified().toString(Qt::ISODateWithMs)},
                        {QStringLiteral("model"), model},
                        {QStringLiteral("duration"), duration},
                        {QStringLiteral("complete"), complete},
                        {QStringLiteral("covered"), covered},
                        {QStringLiteral("segments"), segs}};
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) < 0 || !f.commit()) {
        if (error)
            *error = QObject::tr("无法保存进度：%1").arg(f.errorString());
        return false;
    }
    return true;
}

void Project::remove(const QString& video)
{
    QFile::remove(fileFor(video));
}
