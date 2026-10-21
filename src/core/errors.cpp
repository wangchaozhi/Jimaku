#include "errors.h"

#include <QObject>

namespace {

QString defaultHint(TaskError::Kind kind)
{
    switch (kind) {
    case TaskError::Input:
        return QObject::tr("确认文件存在、能正常播放，并且包含音轨");
    case TaskError::Tool:
        return QObject::tr("需要 ffmpeg：macOS 运行 brew install ffmpeg，Windows 把 ffmpeg.exe 放到程序旁边");
    case TaskError::Model:
        return QObject::tr("在设置里重新下载识别模型");
    case TaskError::OutOfMemory:
        return QObject::tr("内存或显存不足，在设置里换一个小一点的识别模型");
    case TaskError::Auth:
        return QObject::tr("翻译服务拒绝了请求：在设置里检查接口地址、模型名、API key 和账户余额");
    case TaskError::Network:
        return QObject::tr("检查网络连接；用本地 Ollama 时确认它已经启动");
    case TaskError::Canceled:
    case TaskError::None:
    case TaskError::Unknown:
        break;
    }
    return QString();
}

} // namespace

TaskError makeError(TaskError::Kind kind, const QString& message)
{
    return TaskError{kind, message, defaultHint(kind)};
}

TaskError classifyError(const QString& message)
{
    const QString low = message.toLower();
    auto has = [&](const char* s) { return low.contains(QLatin1String(s)); };

    TaskError::Kind kind = TaskError::Unknown;
    if (has("http 404") || has("http 401") || has("http 402") || has("http 403"))
        kind = TaskError::Auth;
    else if (has("ffmpeg") || has("ffprobe"))
        kind = TaskError::Tool;
    else if (has("out of memory") || has("failed to allocate"))
        kind = TaskError::OutOfMemory;
    else if (has("401") || has("403") || has("unauthorized") || has("api key") || has("authentication")
             || has("insufficient balance") || has("402"))
        kind = TaskError::Auth;
    else if (has("connection refused") || has("timed out") || has("timeout") || has("host not found")
             || has("network") || has("unreachable") || has("ssl"))
        kind = TaskError::Network;
    else if (has("model"))
        kind = TaskError::Model;
    else if (has("no such file") || has("not found") || has("invalid data") || has("does not contain")
             || message.contains(QStringLiteral("不存在")))
        kind = TaskError::Input;
    return makeError(kind, message);
}
