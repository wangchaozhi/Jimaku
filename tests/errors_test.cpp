#include "check.h"
#include "core/errors.h"

int main()
{
    expectEq("auth 401", int(classifyError(QStringLiteral("HTTP 401 Authentication Fails")).kind), int(TaskError::Auth));
    expectEq("auth 404 wrong model", int(classifyError(QStringLiteral("HTTP 404 Model Not Exist")).kind), int(TaskError::Auth));
    expectEq("auth balance", int(classifyError(QStringLiteral("HTTP 402 Insufficient Balance")).kind), int(TaskError::Auth));
    expectEq("network", int(classifyError(QStringLiteral("Connection refused (http://127.0.0.1:11434/v1)")).kind), int(TaskError::Network));
    expectEq("tool", int(classifyError(QStringLiteral("找不到 ffmpeg")).kind), int(TaskError::Tool));
    expectEq("input", int(classifyError(QStringLiteral("文件不存在：/a.mp4")).kind), int(TaskError::Input));
    check("hint present", !classifyError(QStringLiteral("HTTP 401")).hint.isEmpty());
    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
