#pragma once

// 无界面模式：Jimaku --cli 视频 [选项]。识别 + 断句 + 翻译后写出字幕，统计信息以 JSON 打到标准输出。
// 退出码：0 成功；1 失败；2 字幕已生成但有行没翻译成功
int runCli(int argc, char** argv);
