#pragma once

// 极简断言：失败打印并计数，main 最后返回失败数（同 SnapPin 的测试写法）
#include <QString>
#include <cstdio>

inline int g_failures = 0;

inline void check(const char* name, bool ok, const QString& detail = QString())
{
    if (ok) {
        std::printf("ok   %s\n", name);
    } else {
        std::printf("FAIL %s %s\n", name, qPrintable(detail));
        ++g_failures;
    }
}

template <typename T>
void expectEq(const char* name, const T& got, const T& want)
{
    check(name, got == want, QStringLiteral("got [%1] want [%2]").arg(got).arg(want));
}
