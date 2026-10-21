#pragma once

#include <QString>

// 处理长视频时阻止系统空闲睡眠（屏幕可以关）；对象销毁时恢复
class SleepGuard {
public:
    explicit SleepGuard(const QString& reason);
    ~SleepGuard();
    SleepGuard(const SleepGuard&) = delete;
    SleepGuard& operator=(const SleepGuard&) = delete;

private:
    unsigned int m_assertion = 0; // macOS IOPMAssertionID
};
