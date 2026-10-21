#pragma once

#include <QString>
#include <QStringList>

// 比较 "1.2.3" / "v1.2.3" 这样的版本号：a < b 返回负数，相等 0，a > b 正数
inline int compareVersions(QString a, QString b)
{
    auto parts = [](QString v) {
        if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
            v.remove(0, 1);
        QList<int> out;
        for (const QString& p : v.split(QLatin1Char('.')))
            out << p.toInt();
        while (out.size() < 3)
            out << 0;
        return out;
    };
    const QList<int> x = parts(a), y = parts(b);
    for (int i = 0; i < 3; ++i) {
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}
