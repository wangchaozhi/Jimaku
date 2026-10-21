#pragma once

#include <QString>

// 失败原因分类：界面按类别给出可操作的提示（移植自 JPSUB 的 orchestrator._classify）
struct TaskError {
    enum Kind { None, Input, Tool, Model, OutOfMemory, Auth, Network, Canceled, Unknown };
    Kind kind = None;
    QString message; // 原始错误信息
    QString hint;    // 给用户的下一步建议

    bool isError() const { return kind != None; }
};

// 按错误信息里的关键词归类，并补上中文提示
TaskError classifyError(const QString& message);

// 已知类别直接构造（hint 用该类别的默认提示）
TaskError makeError(TaskError::Kind kind, const QString& message);
