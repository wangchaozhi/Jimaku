#pragma once

#include <QString>
#include <atomic>
#include <functional>
#include <vector>

// 用外部 ffmpeg/ffprobe 读媒体文件（不链接 libav，发布时只需带两个可执行文件）
namespace MediaDecoder {

// 依次在：程序目录（macOS 还有 .app/Contents/Resources/bin）→ PATH → Homebrew 目录 里找；找不到返回空
QString findTool(const QString& name);

// 媒体总时长（秒）；失败返回负数并写 error
double probeDuration(const QString& path, QString* error);

// 解码成 whisper 需要的 16 kHz 单声道 float PCM。
// progress 参数为 0..1（按已解码时长 / 总时长估算）；cancel 置位后尽快返回 false
bool decodePcm(const QString& path, double duration, std::vector<float>* pcm,
               const std::function<void(double)>& progress, const std::atomic_bool* cancel, QString* error);

// 只解码 [start, start + length) 这一段（长视频分段识别，内存只占一段）。到文件末尾时可能比 length 短
bool decodeRange(const QString& path, double start, double length, std::vector<float>* pcm,
                 const std::atomic_bool* cancel, QString* error);

// 在 pcm 的 [from, to) 秒范围里找最安静的位置（按 50 ms 能量取 0.5 s 窗口的最小值），返回秒数。
// 分段时在这里切开，不会把一个词切成两半
double quietestPoint(const std::vector<float>& pcm, double from, double to);

constexpr int kSampleRate = 16000;

} // namespace MediaDecoder
