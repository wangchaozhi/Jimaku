#include "sleepguard.h"

#include <QtGlobal>

#if defined(Q_OS_MACOS)
#include <IOKit/pwr_mgt/IOPMLib.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

SleepGuard::SleepGuard(const QString& reason)
{
#if defined(Q_OS_MACOS)
    const CFStringRef name = reason.toCFString();
    IOPMAssertionID id = 0;
    if (IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn, name, &id)
        == kIOReturnSuccess)
        m_assertion = id;
    CFRelease(name);
#elif defined(Q_OS_WIN)
    Q_UNUSED(reason);
    // 只对调用线程有效：在处理线程里创建
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
#else
    Q_UNUSED(reason);
#endif
}

SleepGuard::~SleepGuard()
{
#if defined(Q_OS_MACOS)
    if (m_assertion)
        IOPMAssertionRelease(m_assertion);
#elif defined(Q_OS_WIN)
    SetThreadExecutionState(ES_CONTINUOUS);
#endif
}
